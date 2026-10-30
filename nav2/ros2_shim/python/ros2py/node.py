"""A node: the publishers, subscriptions and timers that live under one name.

This is the shim's answer to ``rclpy.node.Node``, and it is deliberately the
real thing rather than a mock: the node writes its own record into the graph
registry (so ``ros2 node list`` and ``ros2 topic list`` see it, and so the
bridge daemon knows the topic exists and can mirror it), and every publisher and
subscription is a named shared-memory channel that the ported C++ nodes read and
write.

The one thing a node does not have is a thread. Nothing here ever blocks: an
:class:`~ros2py.executor.Executor` asks each node to ``poll()``, the node takes
whatever is in its rings right now and returns, and the executor decides how
long to wait before asking again. That is what makes a mission script and its
callbacks run on one thread in a defined order - the order the rclpy executor
gives them, which the scripts were written against.
"""
from __future__ import annotations

import os
import time

from . import channel, clock, graph
from .clock import Clock
from .logging import Logger
from .message_types import message_type_name
from .qos import profile_from

# How often a subscription that found no channel yet looks again. A subscriber
# may well be created before its publisher - a mission node subscribing to
# `/cmd_vel` usually is - and the channel only exists once the publisher makes
# it. Cheap enough to be a plain retry, slow enough not to be a syscall storm.
REOPEN_INTERVAL = 0.2

_NODES = []

# `use_sim_time` is the one parameter the shim has to *act* on rather than store,
# and it is also the one a caller sets from the command line - so the command
# line has to reach a node that has not been built yet. `rclpy.init()` parses
# `--ros-args -p name:=value` into this table, and every node created afterwards
# starts with it. rclpy keeps the same thing in the context; here there is one
# process per script, so a module is enough.
_PARAMETER_OVERRIDES = {}


def set_parameter_overrides(overrides) -> None:
    _PARAMETER_OVERRIDES.clear()
    _PARAMETER_OVERRIDES.update(overrides or {})


def parameter_overrides() -> dict:
    return dict(_PARAMETER_OVERRIDES)


# `use_sim_time` reached a node from the command line (above) or from here.
# The environment is the second way in, and it exists because a script is free
# to filter its own argv before handing it to `rclpy.init` - `go.py` keeps only
# the arguments that start with a dash, so `-p use_sim_time:=true` loses its
# value on the way in - and because a launcher (`shim_run.py --sim-time`) knows
# something the script does not: whether the stack it is about to talk to is
# running on a simulator's clock. The command line wins where the two disagree.
SIM_TIME_ENVIRONMENT = "ROS2_SHIM_SIM_TIME"


def sim_time_from_environment():
    """`use_sim_time` as the environment asks for it, or None where it is silent."""
    raw = os.environ.get(SIM_TIME_ENVIRONMENT)
    if raw is None or not str(raw).strip():
        return None
    return _truthy(raw)


def _coerce(text: str):
    """`true` / `42` / `3.5` / `'text'`, the way a parameter override reads."""
    value = str(text).strip()
    if value in ("true", "True"):
        return True
    if value in ("false", "False"):
        return False
    if len(value) >= 2 and value[0] == value[-1] and value[0] in "\"'":
        return value[1:-1]
    for cast in (int, float):
        try:
            return cast(value)
        except ValueError:
            continue
    return value


def _split_parameter(text: str) -> dict:
    name, separator, value = str(text).partition(":=")
    if not separator:
        return {}
    return {name.lstrip("/"): _coerce(value)}


def overrides_from_args(args) -> dict:
    """Every `-p name:=value` after `--ros-args` in *args*.

    Only `-p` is honoured. `-r`, `--params-file` and the node name and namespace
    arguments are recognised and stepped over rather than misread as
    parameters: the shim's remapping, naming and params files live elsewhere
    (``NAV2_SHM_NAMESPACE``, the node's own name, ``run_nav2_stack.py``), and a
    silently ignored `-r` is a caller who believes a topic moved when it did
    not. Everything after ``--ros-args`` is left for
    ``rclpy.utilities.remove_ros_args()`` to drop, as before.
    """
    overrides = {}
    if args is None:
        return overrides
    tokens = [str(argument) for argument in args]
    if "--ros-args" not in tokens:
        return overrides
    tail = tokens[tokens.index("--ros-args") + 1:]
    index = 0
    while index < len(tail):
        token = tail[index]
        if token in ("-p", "--param"):
            index += 1
            if index < len(tail):
                overrides.update(_split_parameter(tail[index]))
        elif token.startswith("-p") and ":=" in token:
            overrides.update(_split_parameter(token[2:]))
        elif token.startswith("--param="):
            overrides.update(_split_parameter(token[len("--param="):]))
        elif token in ("-r", "--remap", "--params-file", "-n", "__ns"):
            index += 1
        index += 1
    return overrides


class Publisher:
    """One writer of one topic.

    A channel has exactly one writer, which is the transport's rule and not
    this class's. Two of these on one topic is two publish cursors on one ring
    and neither can be read back reliably; wait for the owner to stop, or remap
    the other one somewhere else.
    """

    def __init__(self, node, message_type, topic, qos=None):
        self.node = node
        self.message_type = message_type
        self.topic_name = topic
        self.type_name = message_type_name(message_type)
        self.qos = profile_from(qos)
        self._channel = channel.Channel.create(topic)
        node._registry.add_topic(topic, self.type_name, "pub")

    def publish(self, message) -> bool:
        if self._channel is None:
            self._channel = channel.Channel.create(self.topic_name)
        if self._channel is None:
            return False
        return self._channel.publish(message.serialize())

    def get_topic_name(self) -> str:
        return self.topic_name

    def destroy(self) -> None:
        if self._channel is not None:
            self._channel.close()
            self._channel = None


class Subscription:
    """One reader of one topic, and the callback its messages go to."""

    def __init__(self, node, message_type, topic, callback, qos=None):
        self.node = node
        self.message_type = message_type
        self.topic_name = topic
        self.callback = callback
        self.type_name = message_type_name(message_type)
        self.qos = profile_from(qos)
        self._channel = channel.Channel.open(topic)
        if self._channel is not None:
            # Volatile durability, which is what ROS2 means by a plain
            # subscription: whatever was in the ring before this subscription
            # existed is not this subscription's mail. The ring keeps its
            # contents after its writer exits, so without this a node that
            # restarts reads a previous run's last message as if it were news.
            self._channel.drain()
        self._next_open = time.monotonic() + REOPEN_INTERVAL
        node._registry.add_topic(topic, self.type_name, "sub")

    def poll(self) -> int:
        if self._channel is None:
            now = time.monotonic()
            if now >= self._next_open:
                self._next_open = now + REOPEN_INTERVAL
                self._channel = channel.Channel.open(self.topic_name)
                if self._channel is not None:
                    self._channel.drain()
            if self._channel is None:
                return 0
        delivered = 0
        while True:
            data = self._channel.poll()
            if data is None:
                break
            try:
                message = self.message_type.from_bytes(data)
            except Exception as error:  # noqa: BLE001 - a bad message is not fatal
                self.node.get_logger().error(
                    "dropped a message on %s: %s" % (self.topic_name, error))
                continue
            self.callback(message)
            delivered += 1
        return delivered

    def get_topic_name(self) -> str:
        return self.topic_name

    def destroy(self) -> None:
        if self._channel is not None:
            self._channel.close()
            self._channel = None


class Timer:
    """``create_timer``'s product: a callback that is due every period."""

    def __init__(self, node, period_sec, callback, clock=None):
        self.node = node
        self.period = float(period_sec)
        self.callback = callback
        self.clock = clock
        self._next = time.monotonic() + self.period
        self._cancelled = False

    def is_ready(self, now: float) -> bool:
        return not self._cancelled and now >= self._next

    def call(self) -> None:
        # Scheduled from *this* moment rather than from the previous deadline:
        # a callback that overran must not queue up a burst of catch-up calls,
        # which is what rclpy's timer does too.
        self._next = time.monotonic() + self.period
        self.callback()

    def cancel(self) -> None:
        self._cancelled = True

    def reset(self) -> None:
        self._cancelled = False
        self._next = time.monotonic() + self.period

    def is_canceled(self) -> bool:
        return self._cancelled

    def time_until_next_call(self) -> float:
        return max(0.0, self._next - time.monotonic())

    def destroy(self) -> None:
        self._cancelled = True


def _truthy(value) -> bool:
    """A parameter value as a flag: booleans, the strings rcl accepts, numbers."""
    if isinstance(value, str):
        return value.strip().lower() in ("true", "1", "yes", "on")
    return bool(value)


class Parameter:
    """``get_parameter``'s answer: a name and a value, nothing more needed."""

    def __init__(self, name, value):
        self.name = name
        self.value = value

    def __repr__(self):
        return "Parameter(%r, %r)" % (self.name, self.value)


class Node:
    """One name in the graph, and everything created under it."""

    def __init__(
            self, node_name, namespace=None, context=None, cli_args=None,
            use_global_arguments=True, enable_rosout=True,
            start_parameter_services=True, parameter_overrides=None,
            allow_undeclared_parameters=False,
            automatically_declare_parameters_from_overrides=False):
        self._name = str(node_name).lstrip("/")
        self._namespace = channel.resolve_name(namespace or "/", "/")
        self._registry = graph.ProcessRegistry()
        self._registry.add_node(self.get_fully_qualified_name())
        self._publishers = []
        self._subscriptions = []
        self._timers = []
        self._clients = []
        self._action_clients = []
        # `-p name:=value` and `parameter_overrides` are the same list spelled
        # two ways, and seeding the table with them is what makes
        # `declare_parameter` answer with the override rather than the default -
        # rclpy's rule, and the one a script that reads `use_sim_time` back
        # depends on.
        overrides = dict(_PARAMETER_OVERRIDES)
        overrides.update(parameter_overrides or {})
        self._parameters = dict(overrides)
        self._logger = Logger(self.get_fully_qualified_name())
        self._clock = Clock()
        self._clock_subscription = None
        self._closed = False
        _NODES.append(self)
        # rclpy points a node's ROS clock at `/clock` when `use_sim_time` is
        # true. A script that compares a stamp it read off the wire against its
        # own clock needs the same switch, because the nav2 nodes on this shim
        # run on simulation time (`nav.yaml` sets it for every one of them) and
        # so the stamps they publish are simulation time too.
        if "use_sim_time" in self._parameters:
            self._apply_use_sim_time(self._parameters["use_sim_time"])

    # ------------------------------------------------------------- identity
    def get_name(self) -> str:
        return self._name

    def get_namespace(self) -> str:
        return self._namespace

    def get_fully_qualified_name(self) -> str:
        return self._namespace.rstrip("/") + "/" + self._name

    def get_logger(self) -> Logger:
        return self._logger

    def get_clock(self) -> Clock:
        return self._clock

    def resolve_name(self, name: str) -> str:
        """A name as this node records it: absolute, one leading slash."""
        return channel.resolve_name(name, self._namespace)

    # ----------------------------------------------------------- graph view
    # The introspection `rclpy.node.Node` answers from rmw's cache. Here it is
    # the same merged registry `ros2 topic list` reads, so a node asking about
    # the graph and the CLI printing it cannot disagree - which is the point of
    # one registry rather than a view per process.
    #
    # Every call re-reads it: another process may have appeared since the last
    # question, and a cached answer to "is that service up yet" is exactly the
    # staleness the callers of these are written to wait out.
    def get_topic_names_and_types(self):
        """``[(topic, [type])]`` for every topic in the graph."""
        return graph.read().topic_names_and_types()

    def get_service_names_and_types(self):
        """``[(service, [type])]`` for every service in the graph.

        `nav2_simple_commander` is the caller that matters: to start or stop the
        stack it looks for a service of type
        `nav2_msgs/srv/ManageLifecycleNodes` in here rather than guessing the
        name, because upstream lets the launch file name it.
        """
        return graph.read().service_names_and_types()

    def get_node_names(self):
        """Every node the graph knows about, this one included."""
        return graph.read().node_names()

    def get_node_names_and_namespaces(self):
        """``[(name, namespace)]``, the pair rmw reports rather than the long name."""
        pairs = []
        for full_name in graph.read().node_names():
            namespace, _, name = full_name.rpartition("/")
            pairs.append((name, namespace or "/"))
        return pairs

    def count_publishers(self, topic_name) -> int:
        """Writers of this topic, this process' and every other one's."""
        return graph.read().publisher_count(self.resolve_name(topic_name))

    def count_subscribers(self, topic_name) -> int:
        """Readers of this topic, this process' and every other one's."""
        return graph.read().subscription_count(self.resolve_name(topic_name))

    # ------------------------------------------------------------ factories
    def create_publisher(self, message_type, topic, qos=None, **kwargs) -> Publisher:
        publisher = Publisher(self, message_type, self.resolve_name(topic), qos)
        self._publishers.append(publisher)
        return publisher

    def create_subscription(self, message_type, topic, callback, qos=None,
                            **kwargs) -> Subscription:
        subscription = Subscription(self, message_type, self.resolve_name(topic),
                                    callback, qos)
        self._subscriptions.append(subscription)
        return subscription

    def create_timer(self, timer_period_sec, callback, callback_group=None,
                     clock=None) -> Timer:
        timer = Timer(self, timer_period_sec, callback)
        self._timers.append(timer)
        return timer

    def create_client(self, service_type, service_name, qos=None, **kwargs):
        from .service import Client
        client = Client(self, service_type, service_name, qos)
        self._clients.append(client)
        return client

    # ----------------------------------------------------------- parameters
    def declare_parameter(self, name, value=None, descriptor=None):
        if name not in self._parameters:
            self._parameters[name] = value
            if name == "use_sim_time":
                self._apply_use_sim_time(self._parameters[name])
        return Parameter(name, self._parameters[name])

    def declare_parameters(self, namespace, parameters):
        declared = []
        base = str(namespace).strip("/")
        for name, value in parameters:
            declared.append(self.declare_parameter(
                (base + "." + name) if base else name, value))
        return declared

    def has_parameter(self, name) -> bool:
        return name in self._parameters

    def get_parameter(self, name) -> Parameter:
        return Parameter(name, self._parameters.get(name))

    def get_parameters(self, names):
        return [Parameter(name, self._parameters.get(name)) for name in names]

    def set_parameters(self, parameters):
        results = []
        for parameter in parameters:
            self._parameters[parameter.name] = parameter.value
            if parameter.name == "use_sim_time":
                self._apply_use_sim_time(parameter.value)
            results.append(_SetResult(True, "ok"))
        return results

    def set_parameter(self, parameter):
        return self.set_parameters([parameter])[0]

    def undeclare_parameter(self, name) -> None:
        self._parameters.pop(name, None)

    def get_parameter_names(self):
        return sorted(self._parameters)

    # --------------------------------------------------------- simulated time
    def _apply_use_sim_time(self, value) -> None:
        """Take `use_sim_time` the way rclpy does: switch the ROS clock to the
        one `/clock` carries, and subscribe to it while the switch is on.

        Turning it off is one line; turning it on is idempotent, because the
        switch is process-wide (`ros2py.clock`) and a second node asking for it
        must not open a second `/clock` subscription - a channel has one reader
        per node and two subscriptions to one topic in one node would split the
        stream between them.
        """
        if not _truthy(value):
            clock.set_use_sim_time(False)
            return
        clock.set_use_sim_time(True)
        if self._clock_subscription is not None:
            return
        try:
            from pymessages.rosgraph_msgs.msg.clock import Clock as ClockMessage
        except ImportError as error:  # noqa: BLE001 - a missing type, not a crash
            self.get_logger().warn(
                "use_sim_time is on but /clock cannot be read: %s" % error)
            return
        self._clock_subscription = self.create_subscription(
            ClockMessage, "/clock", self._on_clock_message, 10)

    def _on_clock_message(self, message) -> None:
        clock.set_sim_time(message.clock.sec * 10 ** 9 + message.clock.nanosec)

    # ----------------------------------------------------------------- spin
    def poll(self) -> int:
        """One pass over everything this node owns. Never blocks."""
        handled = 0
        for subscription in list(self._subscriptions):
            handled += subscription.poll()
        for client in list(self._clients):
            client.poll()
        for action_client in list(self._action_clients):
            action_client.poll()
        now = time.monotonic()
        for timer in list(self._timers):
            if timer.is_ready(now):
                timer.call()
                handled += 1
        return handled

    def destroy_node(self) -> None:
        if self._closed:
            return
        self._closed = True
        for publisher in self._publishers:
            publisher.destroy()
        for subscription in self._subscriptions:
            subscription.destroy()
        for timer in self._timers:
            timer.destroy()
        for client in self._clients:
            client.destroy()
        for action_client in self._action_clients:
            action_client.destroy()
        self._publishers.clear()
        self._subscriptions.clear()
        self._timers.clear()
        self._clients.clear()
        self._action_clients.clear()
        self._registry.close()
        if self in _NODES:
            _NODES.remove(self)

    def destroy(self) -> None:
        self.destroy_node()

    def __enter__(self):
        return self

    def __exit__(self, *_exception):
        self.destroy_node()


class _SetResult:
    def __init__(self, successful, reason):
        self.successful = successful
        self.reason = reason


def nodes():
    """Every live node in this process, for the executor to poll."""
    return list(_NODES)
