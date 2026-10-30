"""The shim's graph registry, as a Python process sees it.

The registry is how every process in a shim stack - the ported nav2 nodes, the
gRPC daemon, the ``ros2`` CLI - tells the others which nodes, topics, services
and actions it owns. There is no DDS discovery in this stack, so this is the
whole of it:

    <NAV2_SHM_GRAPH_DIR, default <temp>/nav2_shim_graph[_<NAMESPACE>]>/<pid>.graph

One small tab-separated text file per process, written by its owner alone and
merged by every reader. The format lives in
``nav2/ros2_shim/include/ros2_shim/graph.hpp``, which is where the shim's own
writers and readers spell it; this module is the Python spelling of the same
thing. A Python node that wants ``ros2 topic list`` to show its topics, or the
bridge daemon to mirror them, has to write one of these files, because nothing
else announces it.

Why not shell out to ``ros2 topic pub`` instead? Because a subscription is a
*live* record: it has to appear when the node is created and go away when the
node exits, and a mission node that runs for eight seconds cannot afford a
subprocess - or a graph rewrite - per change.
"""
from __future__ import annotations

import os
import tempfile
from pathlib import Path

HEADER = "#nav2shim-graph\t1"
TERMINATOR = "#end"


def namespace() -> str:
    """``NAV2_SHM_NAMESPACE``, the suffix that separates two stacks."""
    return os.environ.get("NAV2_SHM_NAMESPACE", "")


def temp_directory() -> str:
    for name in ("TMPDIR", "TEMP", "TMP"):
        value = os.environ.get(name)
        if value:
            return value
    return tempfile.gettempdir()


def graph_dir() -> Path:
    """Where the registry lives, namespace and all - the reader's and writer's.

    The namespace is appended rather than prefixed, and to the *override* as
    well as to the default, because the C++ side does it that way: a process
    started with a namespace has to be invisible to one started without, in the
    files as well as in the shared memory.
    """
    override = os.environ.get("NAV2_SHM_GRAPH_DIR", "")
    base = Path(override) if override else Path(temp_directory()) / "nav2_shim_graph"
    name_space = namespace()
    return Path(str(base) + "_" + name_space) if name_space else base


def sanitize(value) -> str:
    """A value that cannot break the record it is written into.

    The format is tab separated and line terminated, so a tab or a newline
    inside a name would be read back as two fields or two records.
    """
    return str(value).replace("\t", " ").replace("\n", " ").replace("\r", " ")


def _windows_reading(path: Path):
    """The file's bytes, opened the way the registry's writer can live with.

    Windows decides whether a *reader* blocks a replace: an `open()` shares
    read and write but not delete, and the writer replaces its file by rename.
    The C++ side opens through ``CreateFileW`` with ``FILE_SHARE_DELETE`` for
    exactly this reason - 15% of its rewrites were lost against a reader that
    did not - so this does too.
    """
    import ctypes
    from ctypes import wintypes

    GENERIC_READ = 0x80000000
    FILE_SHARE_READ, FILE_SHARE_WRITE, FILE_SHARE_DELETE = 0x1, 0x2, 0x4
    OPEN_EXISTING = 3
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    handle = kernel32.CreateFileW(
        str(path), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        None, OPEN_EXISTING, 0, None)
    if handle == -1 or handle is None:
        return None
    try:
        chunks = []
        buffer = ctypes.create_string_buffer(4096)
        got = wintypes.DWORD(0)
        while kernel32.ReadFile(handle, buffer, 4096, ctypes.byref(got), None):
            if got.value == 0:
                break
            chunks.append(buffer.raw[:got.value])
        return b"".join(chunks)
    finally:
        kernel32.CloseHandle(handle)


def read_shared_bytes(path: Path):
    """The bytes of *path*, or None when it is not there to read.

    A name that is missing is not an error and must not be read as "this
    process serves nothing": it is a swap in flight (microseconds) or a file
    that has just been swept. The caller retries.
    """
    if os.name == "nt":
        return _windows_reading(path)
    try:
        with open(path, "rb") as handle:
            return handle.read()
    except OSError:
        return None


def process_gone(pid: int) -> bool:
    """Has the process that wrote this file exited?

    A file whose owner is gone is swept rather than read: a killed node's
    records would otherwise stay on the graph for good. On Windows this is
    ``OpenProcess`` + ``GetExitCodeProcess``, the same pair the C++ side uses -
    and the same pair that tells a live process from an entry ``taskkill`` left
    behind (see ``bridge/scripts/_bridge_platform.py``).
    """
    if pid <= 0:
        return False
    if os.name != "nt":
        try:
            os.kill(pid, 0)
        except ProcessLookupError:
            return True
        except PermissionError:
            return False
        except OSError:
            return True
        return False
    import ctypes
    from ctypes import wintypes

    PROCESS_QUERY_LIMITED_INFORMATION = 0x1000
    STILL_ACTIVE = 259
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    handle = kernel32.OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, False, pid)
    if not handle:
        return ctypes.get_last_error() == 87  # ERROR_INVALID_PARAMETER: no such process
    try:
        code = wintypes.DWORD(0)
        alive = bool(kernel32.GetExitCodeProcess(handle, ctypes.byref(code)))
        return not (alive and code.value == STILL_ACTIVE)
    finally:
        kernel32.CloseHandle(handle)


class Topic:
    def __init__(self, name, type_name, publishers, subscribers, source):
        self.name = name
        self.type = type_name
        self.publishers = publishers
        self.subscribers = subscribers
        self.source = source

    def __repr__(self):
        return "Topic(%r, %r, pub=%d, sub=%d, %r)" % (
            self.name, self.type, self.publishers, self.subscribers, self.source)


class Entry:
    """A service or an action: a name, a type, and who is serving it."""

    def __init__(self, name, type_name, source):
        self.name = name
        self.type = type_name
        self.source = source

    def __repr__(self):
        return "Entry(%r, %r, %r)" % (self.name, self.type, self.source)


class Graph:
    """Every live process' records, merged the way the C++ reader merges them.

    Counts add up across records: two processes publishing the same topic is
    two publishers, which is what nav2's subscriber gate reads when it decides
    whether anyone is listening.
    """

    def __init__(self):
        self.nodes = {}          # name -> source
        self.topics = {}         # (name, source) -> Topic
        self.services = {}       # (name, source) -> Entry
        self.actions = {}        # (name, source) -> Entry

    def topic(self, name):
        for (topic_name, _), topic in self.topics.items():
            if topic_name == name:
                return topic
        return None

    def has_topic(self, name):
        return any(topic_name == name for topic_name, _ in self.topics)

    def has_service(self, name):
        return any(service_name == name for service_name, _ in self.services)

    def has_action(self, name):
        return any(action_name == name for action_name, _ in self.actions)

    def action_type(self, name):
        for (action_name, _), entry in self.actions.items():
            if action_name == name:
                return entry.type
        return ""

    def subscription_count(self, name):
        return sum(t.subscribers for (topic_name, _), t in self.topics.items()
                   if topic_name == name)

    def publisher_count(self, name):
        return sum(t.publishers for (topic_name, _), t in self.topics.items()
                   if topic_name == name)

    def topic_names(self):
        return sorted({name for name, _ in self.topics})

    def node_names(self):
        return sorted(self.nodes)

    def service_names(self):
        return sorted({name for name, _ in self.services})

    def action_names(self):
        return sorted({name for name, _ in self.actions})

    def names_and_types(self, entries):
        """``[(name, [type])]`` for one of the ``{(name, source): entry}`` tables.

        One name seen from two processes is one row, because a name has one
        type and every writer of it records the same one. Where two records do
        disagree, both types are reported rather than a winner picked: a caller
        comparing types wants to see the disagreement, not to be told about
        whichever record happened to be read first.
        """
        merged = {}
        for (name, _source), entry in entries.items():
            types = merged.setdefault(name, [])
            if entry.type and entry.type not in types:
                types.append(entry.type)
        return [(name, merged[name]) for name in sorted(merged)]

    def topic_names_and_types(self):
        return self.names_and_types(self.topics)

    def service_names_and_types(self):
        return self.names_and_types(self.services)


def read(remove_stale: bool = True) -> Graph:
    """Merge every live process' records into one view.

    Three rules, all of them the writer's convenience: only ``*.graph`` files
    count, a file whose owner has exited is swept and skipped, and a file
    without its trailing ``#end`` is somebody's rewrite in progress and is
    skipped *whole* - half a record set is worse than none.
    """
    graph = Graph()
    directory = graph_dir()
    try:
        items = sorted(directory.iterdir())
    except OSError:
        return graph
    for path in items:
        if path.suffix != ".graph":
            continue
        try:
            pid = int(path.stem)
        except ValueError:
            continue
        if remove_stale and process_gone(pid):
            try:
                path.unlink()
            except OSError:
                pass
            continue
        if pid == os.getpid():
            # This process is its own most accurate source; it may not have
            # flushed yet. The registry instance answers directly below.
            pass
        data = read_shared_bytes(path)
        if not data:
            continue
        own = _own_registry()
        if pid == os.getpid() and own is not None:
            own.merge_into(graph)
            continue
        _merge_file(graph, data)
    return graph


def _merge_file(graph: Graph, data: bytes) -> None:
    topics, nodes, services, actions = [], [], [], []
    terminated = False
    text = data.decode("utf-8", "replace")
    for line in text.splitlines():
        if line == TERMINATOR:
            terminated = True
            break
        if not line or line.startswith("#"):
            continue
        fields = line.split("\t")
        if len(fields) >= 2 and fields[0] == "node":
            nodes.append((fields[1], fields[2] if len(fields) > 2 else "local"))
        elif len(fields) >= 5 and fields[0] == "topic":
            topics.append(Topic(fields[1], fields[2], _int(fields[3]), _int(fields[4]),
                                fields[5] if len(fields) > 5 else "local"))
        elif len(fields) >= 3 and fields[0] == "service":
            services.append(Entry(fields[1], fields[2],
                                  fields[3] if len(fields) > 3 else "local"))
        elif len(fields) >= 3 and fields[0] == "action":
            actions.append(Entry(fields[1], fields[2],
                                 fields[3] if len(fields) > 3 else "local"))
    if not terminated:
        return  # somebody's rewrite, caught in the middle
    for topic in topics:
        merge_topic(graph, topic)
    for name, source in nodes:
        graph.nodes[name] = source
    for entry in services:
        graph.services[(entry.name, entry.source)] = entry
    for entry in actions:
        graph.actions[(entry.name, entry.source)] = entry


def _int(value):
    try:
        return int(value)
    except ValueError:
        return 0


def merge_topic(graph: Graph, incoming: Topic) -> None:
    key = (incoming.name, incoming.source)
    existing = graph.topics.get(key)
    if existing is None:
        graph.topics[key] = incoming
        return
    existing.publishers += incoming.publishers
    existing.subscribers += incoming.subscribers
    if not existing.type and incoming.type:
        existing.type = incoming.type


_OWN = []


def _own_registry():
    return _OWN[0] if _OWN else None


def _set_own(registry) -> None:
    _OWN[:] = [registry]


class ProcessRegistry:
    """This process' own records, and the file the others read them from.

    Every change rewrites ``<pid>.graph`` - through a temporary file, so a
    reader never sees half of one - and the reader above answers for this
    process from these records rather than from the file, exactly as the C++
    side does: a rewrite that has not landed yet must not make a node invisible
    to its own queries.
    """

    def __init__(self):
        self._nodes = {}
        self._topics = {}
        self._services = {}
        self._actions = {}
        self.path = graph_dir() / ("%d.graph" % os.getpid())
        _set_own(self)

    # --------------------------------------------------------------- nodes
    def add_node(self, name, source="local"):
        self._nodes[name] = source
        self.flush()

    def remove_node(self, name):
        self._nodes.pop(name, None)
        self.flush()

    # -------------------------------------------------------------- topics
    def add_topic(self, topic, type_name, role, source="local"):
        """Register one publisher or subscriber of *topic*."""
        key = (topic, source)
        entry = self._topics.get(key)
        if entry is None:
            entry = Topic(topic, "", 0, 0, source)
            self._topics[key] = entry
        # The type on the record is the publisher's: a topic has one type, and
        # a subscription that overwrote it would answer `ros2 topic info` with
        # whatever was created last in this process.
        if type_name and (role == "pub" or not entry.type):
            entry.type = type_name
        if role == "pub":
            entry.publishers += 1
        elif role == "sub":
            entry.subscribers += 1
        self.flush()

    def remove_topic(self, topic, role="", source="local"):
        entry = self._topics.get((topic, source))
        if entry is None:
            return
        if role == "pub":
            entry.publishers = max(0, entry.publishers - 1)
        elif role == "sub":
            entry.subscribers = max(0, entry.subscribers - 1)
        else:
            entry.publishers = entry.subscribers = 0
        if entry.publishers == 0 and entry.subscribers == 0:
            del self._topics[(topic, source)]
        self.flush()

    # --------------------------------------------------- services / actions
    def add_service(self, name, type_name, source="local"):
        self._services[(name, source)] = Entry(name, type_name, source)
        self.flush()

    def add_action(self, name, type_name, source="local"):
        self._actions[(name, source)] = Entry(name, type_name, source)
        self.flush()

    # ------------------------------------------------------------- the file
    def text(self) -> str:
        lines = [HEADER]
        for name, source in self._nodes.items():
            lines.append("node\t%s\t%s" % (sanitize(name), sanitize(source)))
        for topic in self._topics.values():
            lines.append("topic\t%s\t%s\t%d\t%d\t%s" % (
                sanitize(topic.name), sanitize(topic.type), topic.publishers,
                topic.subscribers, sanitize(topic.source)))
        for entry in self._services.values():
            lines.append("service\t%s\t%s\t%s" % (
                sanitize(entry.name), sanitize(entry.type), sanitize(entry.source)))
        for entry in self._actions.values():
            lines.append("action\t%s\t%s\t%s" % (
                sanitize(entry.name), sanitize(entry.type), sanitize(entry.source)))
        lines.append(TERMINATOR)
        return "\n".join(lines) + "\n"

    def merge_into(self, graph: Graph) -> None:
        for name, source in self._nodes.items():
            graph.nodes[name] = source
        for topic in self._topics.values():
            merge_topic(graph, Topic(topic.name, topic.type, topic.publishers,
                                     topic.subscribers, topic.source))
        for entry in self._services.values():
            graph.services[(entry.name, entry.source)] = entry
        for entry in self._actions.values():
            graph.actions[(entry.name, entry.source)] = entry

    def flush(self) -> bool:
        """Rewrite this process' file, atomically, and say whether it landed."""
        try:
            self.path.parent.mkdir(parents=True, exist_ok=True)
            temporary = Path(str(self.path) + ".tmp")
            temporary.write_text(self.text(), encoding="utf-8")
            os.replace(str(temporary), str(self.path))
            return True
        except OSError:
            # A reader on the other side can hold the name for a moment. The
            # next change rewrites the file; the in-memory records above are
            # already what this process answers from.
            return False

    def close(self) -> None:
        """Give up this process' records: the node is going away."""
        _set_own(None)
        try:
            self.path.unlink()
        except OSError:
            pass


# The housekeeping queries the shim's C++ side offers, and the ones a client
# actually needs: "is anyone serving this", "is anyone listening".
def action_exists(name: str) -> bool:
    return read().has_action(name)


def service_exists(name: str) -> bool:
    return read().has_service(name)


def subscription_count(topic: str) -> int:
    return read().subscription_count(topic)


def publisher_count(topic: str) -> int:
    return read().publisher_count(topic)
