"""A tiny in-process pub/sub graph that stands in for a ROS2 graph.

Topics are created lazily: the first ``create_publisher`` or
``create_subscription`` (or ``publish``) for a name registers it.  Each topic
keeps its own message sequence counter, and every subscriber of a topic is
called for every message published on it.

Payloads are CDR bytes (including the 4 byte encapsulation header), which is
exactly what the gRPC bridge forwards, so no re-encoding happens on the hot
path.
"""

from __future__ import annotations

import threading
from dataclasses import dataclass, field
from typing import Callable

from . import messages


@dataclass(frozen=True)
class Message:
    """One message observed on a topic."""

    topic: str
    type: str
    data: bytes
    sequence: int
    stamp_ns: int
    #: Populated by subscriptions created with ``decode=True``.
    decoded: dict | None = None


@dataclass
class Topic:
    """A topic plus the endpoints currently attached to it."""

    name: str
    type: str = ""
    sequence: int = 0
    publishers: list["Publisher"] = field(default_factory=list)
    subscribers: list["Subscription"] = field(default_factory=list)

    @property
    def publisher_count(self) -> int:
        return len(self.publishers)

    @property
    def subscriber_count(self) -> int:
        return len(self.subscribers)


@dataclass
class Service:
    """A service a process in the graph is serving.

    ``handler`` takes the decoded request (a plain dict, or ``{}`` for a
    service whose request has no fields) and returns the decoded response.
    ``None`` marks a service this process only knows about - it was announced
    by a peer rather than served here.
    """

    name: str
    type: str
    request_type: str
    response_type: str
    handler: Callable[[dict], dict] | None = None
    served_locally: bool = True


@dataclass
class ActionServer:
    """An action a process in the graph is serving."""

    name: str
    type: str
    goal_type: str
    feedback_type: str
    result_type: str


class Graph:
    """Thread safe in-process pub/sub graph."""

    def __init__(self) -> None:
        self._lock = threading.RLock()
        self._topics: dict[str, Topic] = {}
        self._services: dict[str, Service] = {}
        self._actions: dict[str, ActionServer] = {}

    # -- registry ----------------------------------------------------------

    def _ensure(self, name: str, type_name: str = "") -> Topic:
        """Return the topic, creating it on first use."""
        topic = self._topics.get(name)
        if topic is None:
            topic = Topic(name=name, type=type_name)
            self._topics[name] = topic
        elif type_name and not topic.type:
            topic.type = type_name
        return topic

    def get_topic(self, name: str) -> Topic | None:
        with self._lock:
            return self._topics.get(name)

    def topic_type(self, name: str) -> str:
        topic = self.get_topic(name)
        return topic.type if topic else ""

    def topics(self) -> list[Topic]:
        """All known topics, sorted by name."""
        with self._lock:
            return [self._topics[name] for name in sorted(self._topics)]

    def subscribe_counts(self) -> dict[str, int]:
        """``{topic: subscriber_count}`` for every known topic."""
        return {topic.name: topic.subscriber_count for topic in self.topics()}

    # -- services ----------------------------------------------------------

    def create_service(
        self,
        name: str,
        type_name: str,
        handler: Callable[[dict], dict],
        *,
        request_type: str = "",
        response_type: str = "",
    ) -> Service:
        """Register a service this process serves."""
        if not request_type or not response_type:
            request_type, response_type = messages.SERVICE_TYPES.get(type_name, ("", ""))
        if not request_type or not response_type:
            raise messages.UnsupportedType(type_name)
        service = Service(
            name=name,
            type=type_name,
            request_type=request_type,
            response_type=response_type,
            handler=handler,
        )
        with self._lock:
            self._services[name] = service
        return service

    def remove_service(self, name: str) -> None:
        with self._lock:
            self._services.pop(name, None)

    def get_service(self, name: str) -> Service | None:
        with self._lock:
            return self._services.get(name)

    def service_type(self, name: str) -> str:
        service = self.get_service(name)
        return service.type if service else ""

    def services(self) -> list[Service]:
        """Every advertised service, sorted by name."""
        with self._lock:
            return [self._services[name] for name in sorted(self._services)]

    # -- actions -----------------------------------------------------------

    def create_action(
        self,
        name: str,
        type_name: str,
        *,
        goal_type: str = "",
        feedback_type: str = "",
        result_type: str = "",
    ) -> ActionServer:
        if not goal_type or not feedback_type or not result_type:
            goal_type, feedback_type, result_type = messages.ACTION_TYPES.get(type_name, ("", "", ""))
        if not goal_type or not feedback_type or not result_type:
            raise messages.UnsupportedType(type_name)
        action = ActionServer(
            name=name,
            type=type_name,
            goal_type=goal_type,
            feedback_type=feedback_type,
            result_type=result_type,
        )
        with self._lock:
            self._actions[name] = action
        return action

    def action_type(self, name: str) -> str:
        with self._lock:
            action = self._actions.get(name)
            return action.type if action else ""

    def actions(self) -> list[ActionServer]:
        with self._lock:
            return [self._actions[name] for name in sorted(self._actions)]

    def remove(self, name: str) -> None:
        """Drop a topic that has no endpoints left."""
        with self._lock:
            topic = self._topics.get(name)
            if topic and not topic.publishers and not topic.subscribers:
                del self._topics[name]

    # -- endpoints ---------------------------------------------------------

    def create_publisher(self, name: str, type_name: str) -> "Publisher":
        with self._lock:
            topic = self._ensure(name, type_name)
            publisher = Publisher(self, topic)
            topic.publishers.append(publisher)
            return publisher

    def create_subscription(
        self,
        name: str,
        type_name: str,
        callback: Callable[[Message], None],
        *,
        decode: bool = False,
    ) -> "Subscription":
        """Register ``callback`` for every message published on ``name``.

        With ``decode=True`` the callback additionally receives the decoded
        message in :attr:`Message.decoded`; undecodable payloads are skipped.
        """
        with self._lock:
            topic = self._ensure(name, type_name)
            subscription = Subscription(self, topic, callback, decode=decode)
            topic.subscribers.append(subscription)
            return subscription

    # -- data path ---------------------------------------------------------

    def publish(
        self,
        name: str,
        type_name: str = "",
        data: bytes | None = None,
        *,
        message: dict | None = None,
        stamp_ns: int = 0,
    ) -> Message:
        """Publish a message and hand it to every subscriber of the topic."""
        with self._lock:
            topic = self._ensure(name, type_name)
            if data is None:
                if message is None:
                    raise ValueError("publish() needs either data= or message=")
                if not type_name:
                    raise ValueError("publish(message=...) needs a type")
                data = messages.encode(message, type_name)
            sequence = topic.sequence
            topic.sequence = sequence + 1
            event = Message(
                topic=topic.name,
                type=topic.type or type_name,
                data=bytes(data),
                sequence=sequence,
                stamp_ns=stamp_ns,
            )
            subscribers = list(topic.subscribers)

        for subscription in subscribers:
            subscription.deliver(event)
        return event


class Publisher:
    """Handle returned by :meth:`Graph.create_publisher`."""

    __slots__ = ("_graph", "_topic")

    def __init__(self, graph: Graph, topic: Topic) -> None:
        self._graph = graph
        self._topic = topic

    def publish(
        self,
        data: bytes | None = None,
        *,
        message: dict | None = None,
        stamp_ns: int = 0,
    ) -> Message:
        """Publish CDR ``data`` or an encodable ``message`` dict."""
        return self._graph.publish(
            self._topic.name, self._topic.type, data, message=message, stamp_ns=stamp_ns
        )

    def unadvertise(self) -> None:
        with self._graph._lock:
            if self in self._topic.publishers:
                self._topic.publishers.remove(self)
        self._graph.remove(self._topic.name)


class Subscription:
    """Handle returned by :meth:`Graph.create_subscription`."""

    __slots__ = ("_graph", "_topic", "_callback", "_decode", "_active")

    def __init__(
        self,
        graph: Graph,
        topic: Topic,
        callback: Callable[[Message], None],
        *,
        decode: bool = False,
    ) -> None:
        self._graph = graph
        self._topic = topic
        self._callback = callback
        self._decode = decode
        self._active = True

    @property
    def active(self) -> bool:
        return self._active

    def deliver(self, event: Message) -> None:
        """Hand ``event`` to the callback (drops it once unsubscribed)."""
        if not self._active:
            return
        if self._decode:
            try:
                event = Message(
                    topic=event.topic,
                    type=event.type,
                    data=event.data,
                    sequence=event.sequence,
                    stamp_ns=event.stamp_ns,
                    decoded=messages.decode(event.data, event.type),
                )
            except (messages.UnsupportedType, ValueError):
                return
        self._callback(event)

    def unsubscribe(self) -> None:
        """Detach from the topic; idempotent."""
        if not self._active:
            return
        self._active = False
        with self._graph._lock:
            if self in self._topic.subscribers:
                self._topic.subscribers.remove(self)
        self._graph.remove(self._topic.name)
