"""gRPC ``ros2bridge.Ros2Bridge`` server backed by the in-process graph.

The agent is deliberately thin: it forwards the CDR bytes that the graph
carries, so a subscriber on the other side of the wire sees exactly the bytes
a local ROS2 node would have seen (including the 4 byte encapsulation header).
"""

from __future__ import annotations

import logging
import re
import threading
import time
import uuid
from collections import deque
from concurrent import futures

import grpc

from . import messages
from .graph import Graph, Message
from .proto import ros2_bridge_pb2 as pb
from .proto import ros2_bridge_pb2_grpc as pb_grpc

LOG = logging.getLogger("ros2_bridge_sim.agent")

AGENT_VERSION = "ros2_bridge_sim/0.1"
ROS_DISTRO = "sim"

DEFAULT_HOST = "0.0.0.0"
DEFAULT_PORT = 50051

#: How many messages a slow subscriber may fall behind before the oldest are
#: dropped (keeps a stalled client from growing the server without bound).
DEFAULT_QUEUE_SIZE = 1024

#: Statuses that mean "this goal will not change again".
_TERMINAL = frozenset(
    (
        pb.GOAL_STATUS_SUCCEEDED,
        pb.GOAL_STATUS_CANCELED,
        pb.GOAL_STATUS_ABORTED,
        pb.GOAL_STATUS_REJECTED,
    )
)


class _MessageQueue:
    """Small bounded queue used to hand graph messages to a streaming RPC."""

    __slots__ = ("_items", "_max", "_cond")

    def __init__(self, maxsize: int = DEFAULT_QUEUE_SIZE) -> None:
        self._items: deque[Message] = deque()
        self._max = max(1, maxsize)
        self._cond = threading.Condition()

    def push(self, event: Message) -> None:
        with self._cond:
            if len(self._items) >= self._max:
                self._items.popleft()
            self._items.append(event)
            self._cond.notify()

    def pop(self, timeout: float) -> Message | None:
        with self._cond:
            if not self._items:
                self._cond.wait(timeout)
            return self._items.popleft() if self._items else None


#: How long a finished goal is kept around for a late GetActionResult.
DEFAULT_RESULT_TTL_S = 60.0


class _Goal:
    """One in-flight (or recently finished) action goal.

    Feedback and the terminal result travel as a stream of events, which is
    what both ``ActionFeedback`` (follow live) and ``GetActionResult`` (poll
    once) read: a client that starts following after the first feedback
    messages were produced still sees them.
    """

    __slots__ = (
        "name", "goal_id", "type", "goal_type", "feedback_type", "result_type",
        "handler", "created", "finished_at", "cancel", "thread", "error",
        "_events", "_cond", "status", "result",
    )

    def __init__(self, name, goal_id, type_name, action_info, handler) -> None:
        self.name = name
        self.goal_id = goal_id
        self.type = type_name
        self.goal_type = action_info.goal_type
        self.feedback_type = action_info.feedback_type
        self.result_type = action_info.result_type
        self.handler = handler
        self.created = time.perf_counter()
        self.finished_at: float | None = None
        self.cancel = threading.Event()
        self.thread: threading.Thread | None = None
        self.error = ""
        self.status = 0  # GOAL_STATUS_UNKNOWN until the goal is accepted
        self.result: bytes | None = None
        self._events: list[tuple[int, bytes | None, str]] = []
        self._cond = threading.Condition()

    # -- producer side -----------------------------------------------------

    def emit(self, status: int, feedback: bytes | None = None) -> None:
        with self._cond:
            self.status = status
            self._events.append((status, feedback, ""))
            self._cond.notify_all()

    def finish(self, status: int, result: bytes | None, error: str = "") -> None:
        with self._cond:
            self.status = status
            self.result = result
            self.error = error
            self.finished_at = time.perf_counter()
            self._events.append((status, None, error))
            self._cond.notify_all()

    @property
    def done(self) -> bool:
        with self._cond:
            return self.finished_at is not None

    # -- consumer side -----------------------------------------------------

    def follow(self, index: int = 0):
        """Yield ``(status, feedback, error)`` from ``index`` until the goal ends."""
        while True:
            with self._cond:
                while index >= len(self._events) and self.finished_at is None:
                    self._cond.wait(0.25)
                events = self._events[index:]
                index = len(self._events)
                done = self.finished_at is not None
            for event in events:
                yield event
            if done:
                return

    def wait(self, timeout: float) -> bool:
        """Block until the goal ends; returns whether it ended in time."""
        deadline = time.perf_counter() + max(0.0, timeout)
        with self._cond:
            while self.finished_at is None:
                remaining = deadline - time.perf_counter()
                if remaining <= 0:
                    return False
                self._cond.wait(min(0.25, remaining))
            return True


class BridgeAgent(pb_grpc.Ros2BridgeServicer):
    """``Ros2Bridge`` service over a :class:`~ros2_bridge_sim.graph.Graph`."""

    def __init__(
        self,
        graph: Graph,
        *,
        agent_version: str = AGENT_VERSION,
        ros_distro: str = ROS_DISTRO,
        queue_size: int = DEFAULT_QUEUE_SIZE,
        result_ttl_s: float = DEFAULT_RESULT_TTL_S,
    ) -> None:
        self.graph = graph
        self.agent_version = agent_version
        self.ros_distro = ros_distro
        self.queue_size = queue_size
        self._lock = threading.Lock()
        self._subscribe_count = 0
        # goal_id -> _Goal. Entries are pruned once the result has been
        # collectable for `result_ttl_s`, so a bridge that runs for days does
        # not accumulate every goal it ever carried.
        self._goals: dict[str, _Goal] = {}
        self._actions: dict[str, object] = {}
        self.result_ttl_s = result_ttl_s

    # -- liveness ----------------------------------------------------------

    def Ping(self, request: pb.PingRequest, context: grpc.ServicerContext) -> pb.PingResponse:
        return pb.PingResponse(
            payload=request.payload,
            agent_version=self.agent_version,
            ros_distro=self.ros_distro,
        )

    # -- discovery ---------------------------------------------------------

    def ListTopics(
        self, request: pb.ListTopicsRequest, context: grpc.ServicerContext
    ) -> pb.ListTopicsResponse:
        regex = None
        if request.name_filter:
            try:
                regex = re.compile(request.name_filter)
            except re.error as exc:
                context.abort(
                    grpc.StatusCode.INVALID_ARGUMENT,
                    "invalid name_filter regex %r: %s" % (request.name_filter, exc),
                )
        topics = [
            pb.TopicInfo(
                name=topic.name,
                type=topic.type,
                publisher_count=topic.publisher_count,
                subscriber_count=topic.subscriber_count,
            )
            for topic in self.graph.topics()
            if regex is None or regex.search(topic.name)
        ]
        return pb.ListTopicsResponse(topics=topics)

    def GetTopicInfo(self, request: pb.GetTopicInfoRequest, context: grpc.ServicerContext) -> pb.TopicInfo:
        topic = self.graph.get_topic(request.name)
        if topic is None:
            context.abort(grpc.StatusCode.NOT_FOUND, "unknown topic %r" % request.name)
        return pb.TopicInfo(
            name=topic.name,
            type=topic.type,
            publisher_count=topic.publisher_count,
            subscriber_count=topic.subscriber_count,
        )

    # -- data path ---------------------------------------------------------

    def Subscribe(self, request: pb.SubscribeRequest, context: grpc.ServicerContext):
        """Stream every message published on ``request.topic``."""
        if not request.topic:
            context.abort(grpc.StatusCode.INVALID_ARGUMENT, "topic must not be empty")

        existing = self.graph.topic_type(request.topic)
        if request.type and existing and request.type != existing:
            context.abort(
                grpc.StatusCode.FAILED_PRECONDITION,
                "topic %r has type %r, not %r" % (request.topic, existing, request.type),
            )
        type_name = request.type or existing

        queue = _MessageQueue(self.queue_size)
        subscription = self.graph.create_subscription(request.topic, type_name, queue.push)
        with self._lock:
            self._subscribe_count += 1
        LOG.debug("subscriber attached to %s (%s)", request.topic, type_name or "untyped")
        try:
            throttle = max(0.0, request.throttle_ms / 1000.0)
            last_sent = 0.0
            while context.is_active():
                event = queue.pop(0.25)
                if event is None:
                    continue
                if throttle:
                    now = time.monotonic()
                    if last_sent and now - last_sent < throttle:
                        continue
                    last_sent = now
                yield pb.Message(
                    topic=event.topic,
                    type=event.type,
                    data=event.data,
                    encoding="cdr",
                    stamp_ns=event.stamp_ns,
                    sequence=event.sequence,
                )
        finally:
            subscription.unsubscribe()
            with self._lock:
                self._subscribe_count -= 1
            LOG.debug("subscriber detached from %s", request.topic)

    def Publish(self, request: pb.PublishRequest, context: grpc.ServicerContext) -> pb.PublishResponse:
        """Accept a CDR payload and deliver it into the graph."""
        encoding = (request.encoding or "cdr").strip().lower()
        if encoding != "cdr":
            return pb.PublishResponse(
                accepted=False,
                error="unsupported encoding %r: only 'cdr' (or empty) is supported" % request.encoding,
            )
        if not request.topic:
            return pb.PublishResponse(accepted=False, error="topic must not be empty")
        data = bytes(request.data)
        if not data:
            return pb.PublishResponse(accepted=False, error="empty payload")

        type_name = self.graph.topic_type(request.topic) or request.type
        if not type_name:
            return pb.PublishResponse(
                accepted=False,
                error="no type known for topic %r; set 'type' in the request" % request.topic,
            )

        try:
            messages.decode(data, type_name)
        except messages.UnsupportedType:
            pass  # unknown schema: forward the bytes untouched
        except ValueError as exc:
            return pb.PublishResponse(
                accepted=False,
                error="undecodable CDR payload for type %r: %s" % (type_name, exc),
            )
        except Exception as exc:  # never let a bad payload kill the server
            LOG.exception("unexpected failure decoding %s", type_name)
            return pb.PublishResponse(
                accepted=False, error="failed to decode %r: %s" % (type_name, exc)
            )

        try:
            self.graph.publish(request.topic, type_name, data, stamp_ns=time.time_ns())
        except Exception as exc:
            LOG.exception("failed to publish on %s", request.topic)
            return pb.PublishResponse(accepted=False, error="publish failed: %s" % exc)
        return pb.PublishResponse(accepted=True)

    # -- services ----------------------------------------------------------

    def register_action(self, name: str, handler) -> None:
        """Attach an implementation to an action the graph advertises.

        ``handler(goal, feedback, cancel)`` receives the decoded goal, a
        callable that publishes decoded feedback, and a ``threading.Event``
        that is set when the caller cancels. It returns ``(result, error)``:
        a decoded result dict, or ``None`` plus a message explaining why the
        goal aborted.
        """
        self._actions[name] = handler

    def ListServices(
        self, request: pb.ListServicesRequest, context: grpc.ServicerContext
    ) -> pb.ListServicesResponse:
        regex = None
        if request.name_filter:
            try:
                regex = re.compile(request.name_filter)
            except re.error as exc:
                context.abort(
                    grpc.StatusCode.INVALID_ARGUMENT,
                    "invalid name_filter regex %r: %s" % (request.name_filter, exc),
                )
        services = [
            pb.ServiceInfo(name=service.name, type=service.type, source="local")
            for service in self.graph.services()
            if regex is None or regex.search(service.name)
        ]
        return pb.ListServicesResponse(services=services)

    def GetServiceInfo(
        self, request: pb.GetServiceInfoRequest, context: grpc.ServicerContext
    ) -> pb.ServiceInfo:
        service = self.graph.get_service(request.name)
        if service is None:
            context.abort(grpc.StatusCode.NOT_FOUND, "unknown service %r" % request.name)
        return pb.ServiceInfo(name=service.name, type=service.type, source="local")

    def CallService(
        self, request: pb.CallServiceRequest, context: grpc.ServicerContext
    ) -> pb.CallServiceResponse:
        """Run one request/response round trip against a service in the graph."""
        started = time.perf_counter_ns()
        service = self.graph.get_service(request.name)
        if service is None or service.handler is None:
            return pb.CallServiceResponse(
                success=False, error="no service named %r" % request.name
            )
        encoding = (request.encoding or "cdr").strip().lower()
        if encoding != "cdr":
            return pb.CallServiceResponse(
                success=False,
                error="unsupported encoding %r: only 'cdr' (or empty) is supported" % request.encoding,
            )
        payload = bytes(request.request)
        if payload:
            try:
                decoded = messages.decode(payload, service.request_type)
            except Exception as exc:
                return pb.CallServiceResponse(
                    success=False,
                    error="undecodable %s request: %s" % (service.request_type, exc),
                )
        else:
            # An empty body only makes sense for a request that has no fields;
            # anything else is a caller that forgot to serialise its request.
            if messages.SCHEMAS.get(service.request_type):
                return pb.CallServiceResponse(
                    success=False,
                    error="empty request body for %s" % service.request_type,
                )
            decoded = {}
        deadline = request.timeout_ms / 1000.0 if request.timeout_ms > 0 else 5.0
        try:
            response = service.handler(decoded)
        except Exception as exc:  # a broken handler must not kill the server
            LOG.exception("service %s failed", request.name)
            return pb.CallServiceResponse(success=False, error="handler failed: %s" % exc)
        if not isinstance(response, dict):
            return pb.CallServiceResponse(
                success=False,
                error="service %r returned %s, expected a dict" % (request.name, type(response).__name__),
            )
        try:
            encoded = messages.encode(response, service.response_type)
        except Exception as exc:
            return pb.CallServiceResponse(
                success=False, error="undecodable %s response: %s" % (service.response_type, exc)
            )
        elapsed = (time.perf_counter_ns() - started) / 1e9
        if elapsed > deadline:
            LOG.debug("service %s took %.3f s (caller budget %.3f s)", request.name, elapsed, deadline)
        return pb.CallServiceResponse(
            success=True,
            response=encoded,
            encoding="cdr",
            elapsed_ns=int(elapsed * 1e9) or 1,
        )

    # -- actions -----------------------------------------------------------

    def ListActions(
        self, request: pb.ListActionsRequest, context: grpc.ServicerContext
    ) -> pb.ListActionsResponse:
        regex = None
        if request.name_filter:
            try:
                regex = re.compile(request.name_filter)
            except re.error as exc:
                context.abort(
                    grpc.StatusCode.INVALID_ARGUMENT,
                    "invalid name_filter regex %r: %s" % (request.name_filter, exc),
                )
        actions = [
            pb.ActionInfo(name=action.name, type=action.type, source="local")
            for action in self.graph.actions()
            if regex is None or regex.search(action.name)
        ]
        return pb.ListActionsResponse(actions=actions)

    def GetActionInfo(
        self, request: pb.GetActionInfoRequest, context: grpc.ServicerContext
    ) -> pb.ActionInfo:
        action = self._find_action(request.name)
        if action is None:
            context.abort(grpc.StatusCode.NOT_FOUND, "unknown action %r" % request.name)
        return pb.ActionInfo(name=action.name, type=action.type, source="local")

    def SendActionGoal(
        self, request: pb.SendActionGoalRequest, context: grpc.ServicerContext
    ) -> pb.SendActionGoalResponse:
        action = self._find_action(request.name)
        if action is None:
            # Same split as the C++ server: a name nobody serves is NOT_FOUND,
            # while an advertised action that refuses the goal answers
            # `accepted=False` with a reason.
            context.abort(grpc.StatusCode.NOT_FOUND, "unknown action %r" % request.name)
        handler = self._actions.get(action.name)
        if handler is None:
            return pb.SendActionGoalResponse(
                accepted=False, error="action %r has no server here" % request.name
            )
        encoding = (request.encoding or "cdr").strip().lower()
        if encoding != "cdr":
            return pb.SendActionGoalResponse(
                accepted=False,
                error="unsupported encoding %r: only 'cdr' (or empty) is supported" % request.encoding,
            )
        try:
            goal = messages.decode(bytes(request.goal), action.goal_type) if request.goal else {}
        except Exception as exc:
            return pb.SendActionGoalResponse(
                accepted=False, error="undecodable %s goal: %s" % (action.goal_type, exc)
            )

        self._prune_goals()
        goal_id = uuid.uuid4().hex
        state = _Goal(action.name, goal_id, action.type, action, handler)
        state.emit(pb.GOAL_STATUS_ACCEPTED)
        with self._lock:
            self._goals[goal_id] = state

        def run() -> None:
            state.emit(pb.GOAL_STATUS_EXECUTING)

            def feedback(payload: dict) -> None:
                try:
                    state.emit(pb.GOAL_STATUS_EXECUTING, messages.encode(payload, action.feedback_type))
                except Exception:
                    LOG.exception("action %s produced undecodable feedback", action.name)

            try:
                result, error = handler(goal, feedback, state.cancel)
            except Exception as exc:
                LOG.exception("action %s failed", action.name)
                result, error = None, "handler failed: %s" % exc
            if state.cancel.is_set():
                # The caller cancelled, so the goal ends as CANCELED whatever
                # the handler returned - but it ends *with* that result. Passing
                # `state.result` here would be passing the value `finish()` is
                # about to store: still None, so every cancelled goal went out
                # with an empty payload and a caller that reads the result
                # waited out its whole timeout for a goal that was already over.
                encoded = None
                if result is not None:
                    try:
                        encoded = messages.encode(result, action.result_type)
                    except Exception:
                        LOG.exception("action %s produced an undecodable result", action.name)
                state.finish(pb.GOAL_STATUS_CANCELED, encoded, error)
                return
            if result is None:
                state.finish(pb.GOAL_STATUS_ABORTED, None, error or "aborted")
                return
            try:
                encoded = messages.encode(result, action.result_type)
            except Exception as exc:
                state.finish(pb.GOAL_STATUS_ABORTED, None, "undecodable result: %s" % exc)
                return
            state.finish(pb.GOAL_STATUS_SUCCEEDED, encoded, error)

        thread = threading.Thread(target=run, name="action-%s" % goal_id, daemon=True)
        state.thread = thread
        thread.start()
        return pb.SendActionGoalResponse(accepted=True, goal_id=goal_id)

    def ActionFeedback(self, request: pb.ActionFeedbackRequest, context: grpc.ServicerContext):
        state = self._require_goal(request.name, request.goal_id)
        if state is None:
            context.abort(
                grpc.StatusCode.NOT_FOUND,
                "no goal %r on action %r" % (request.goal_id, request.name),
            )
        for status, feedback, error in state.follow():
            # `encoding` describes whatever this message carries: feedback
            # while the goal runs, the result on the terminal message. A
            # status-only message (accepted, executing, canceling) carries
            # neither and says so.
            terminal = status in _TERMINAL
            result = (state.result or b"") if terminal else b""
            yield pb.ActionFeedbackMessage(
                name=state.name,
                goal_id=state.goal_id,
                feedback=feedback or b"",
                encoding="cdr" if (feedback or result) else "",
                status=status,
                result=result,
                error=error,
            )

    def GetActionResult(
        self, request: pb.GetActionResultRequest, context: grpc.ServicerContext
    ) -> pb.GetActionResultResponse:
        state = self._require_goal(request.name, request.goal_id)
        if state is None:
            return pb.GetActionResultResponse(
                success=False, error="no goal %r on action %r" % (request.goal_id, request.name)
            )
        timeout = request.timeout_ms / 1000.0 if request.timeout_ms > 0 else 0.0
        state.wait(timeout)
        if state.finished_at is None:
            return pb.GetActionResultResponse(
                success=False, status=state.status, error="goal is still running"
            )
        return pb.GetActionResultResponse(
            success=state.status == pb.GOAL_STATUS_SUCCEEDED,
            error=state.error,
            result=state.result or b"",
            encoding="cdr" if state.result else "",
            status=state.status,
        )

    def CancelActionGoal(
        self, request: pb.CancelActionGoalRequest, context: grpc.ServicerContext
    ) -> pb.CancelActionGoalResponse:
        state = self._require_goal(request.name, request.goal_id)
        if state is None:
            return pb.CancelActionGoalResponse(
                accepted=False, error="no goal %r on action %r" % (request.goal_id, request.name)
            )
        if state.finished_at is not None:
            return pb.CancelActionGoalResponse(
                accepted=False, error="goal %r already finished" % request.goal_id
            )
        state.cancel.set()
        state.emit(pb.GOAL_STATUS_CANCELING)
        return pb.CancelActionGoalResponse(accepted=True)

    # -- goal bookkeeping --------------------------------------------------

    def _find_action(self, name: str):
        for action in self.graph.actions():
            if action.name == name:
                return action
        return None

    def _require_goal(self, name: str, goal_id: str) -> _Goal | None:
        with self._lock:
            state = self._goals.get(goal_id)
        if state is None or (name and state.name != name):
            return None
        return state

    def _prune_goals(self) -> None:
        """Drop goals whose result has been collectable for long enough."""
        now = time.perf_counter()
        with self._lock:
            stale = [
                goal_id
                for goal_id, state in self._goals.items()
                if state.finished_at is not None
                and state.finished_at + self.result_ttl_s <= now
            ]
            for goal_id in stale:
                del self._goals[goal_id]

    @property
    def goal_count(self) -> int:
        with self._lock:
            return len(self._goals)

    # -- introspection -----------------------------------------------------

    @property
    def subscribe_count(self) -> int:
        with self._lock:
            return self._subscribe_count


def build_server(
    graph: Graph,
    host: str = DEFAULT_HOST,
    port: int = DEFAULT_PORT,
    *,
    max_workers: int = 32,
    queue_size: int = DEFAULT_QUEUE_SIZE,
    result_ttl_s: float = DEFAULT_RESULT_TTL_S,
) -> tuple[grpc.Server, BridgeAgent, int]:
    """Create (but do not start) the gRPC server.

    Returns ``(server, agent, bound_port)``; ``bound_port`` is the real port,
    which matters when ``port`` is 0.
    """
    executor = futures.ThreadPoolExecutor(max_workers=max_workers)
    server = grpc.server(executor)
    # `grpc.server` does not own the executor it is handed, so stopping the
    # server leaves its worker threads alive. A test suite that starts a few
    # dozen agents then runs out of threads; `stop_server` shuts this down.
    server.ros2_bridge_executor = executor
    agent = BridgeAgent(graph, queue_size=queue_size, result_ttl_s=result_ttl_s)
    pb_grpc.add_Ros2BridgeServicer_to_server(agent, server)
    bound = server.add_insecure_port("%s:%d" % (host, port))
    if not bound:
        raise RuntimeError("could not bind %s:%d" % (host, port))
    return server, agent, bound


def serve(
    graph: Graph,
    host: str = DEFAULT_HOST,
    port: int = DEFAULT_PORT,
    *,
    max_workers: int = 32,
    block: bool = True,
    grace: float = 1.0,
    on_ready=None,
    result_ttl_s: float = DEFAULT_RESULT_TTL_S,
) -> tuple[grpc.Server, int]:
    """Start the bridge server and optionally block until it terminates."""
    server, _agent, bound = build_server(
        graph, host, port, max_workers=max_workers, result_ttl_s=result_ttl_s
    )
    server.start()
    LOG.info("ros2 bridge agent listening on %s:%d (%s)", host, bound, AGENT_VERSION)
    if on_ready is not None:
        on_ready(bound)
    if block:
        try:
            server.wait_for_termination()
        except KeyboardInterrupt:
            pass
        finally:
            stop_server(server, grace)
    return server, bound


def stop_server(server: grpc.Server, grace: float = 1.0) -> None:
    """Gracefully shut the server down, worker threads included."""
    LOG.info("stopping ros2 bridge agent")
    server.stop(grace).wait(timeout=grace + 5.0)
    executor = getattr(server, "ros2_bridge_executor", None)
    if executor is not None:
        executor.shutdown(wait=True)