"""The navigation stack's launch state machine.

`run_nav2_stack.py` used to report a bringup that did not finish as a warning
and carry on: a goal was then sent at a half-active stack, hung until its
timeout, and the nodes the failed run had already started were left behind for
the next run to trip over. This checks the two things that changed:

  * a failure at any stage takes down the processes that run had started
    (``Startup.rollback``), and
  * the READY stage only happens once ``/navigate_to_pose`` is a server
    (``action_server_ready``), not merely once the lifecycle manager says the
    nodes are active.

The script lives in ``bridge/scripts`` and imports the Windows/POSIX helper
module, which probes the machine on import; nothing here runs a node, so the
helpers it would call are replaced with stubs.
"""

import importlib.util
import sys
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parents[3] / "scripts"


def load_module(monkeypatch, killed):
    """Import run_nav2_stack with bp.kill_nodes recorded instead of acting."""
    sys.path.insert(0, str(SCRIPTS))
    spec = importlib.util.spec_from_file_location(
        "run_nav2_stack_under_test", SCRIPTS / "run_nav2_stack.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)

    def fake_kill(names, note=""):
        killed.append(list(names))
        return True

    monkeypatch.setattr(module.bp, "kill_nodes", fake_kill)
    return module


def test_rollback_nothing_started(monkeypatch):
    killed = []
    module = load_module(monkeypatch, killed)
    startup = module.Startup()
    startup.rollback()
    assert killed == []


def test_rollback_kills_in_reverse(monkeypatch):
    killed = []
    module = load_module(monkeypatch, killed)
    startup = module.Startup()
    for name in ["map_server", "amcl_bridge_node", "planner_server",
                 "lifecycle_manager"]:
        startup.record(name)
    startup.rollback()
    # The manager goes last in the start order, so it goes first in the stop
    # order: it must not reconfigure servers that are disappearing.
    assert killed == [["lifecycle_manager", "planner_server",
                       "amcl_bridge_node", "map_server"]]


def test_startup_enter_tracks_stage(monkeypatch, capsys):
    killed = []
    module = load_module(monkeypatch, killed)
    startup = module.Startup()
    startup.enter("servers")
    assert startup.stage == "servers"
    assert "stage: servers" in capsys.readouterr().out


def test_action_server_ready_true_when_listed(monkeypatch):
    killed = []
    module = load_module(monkeypatch, killed)

    class Done:
        stdout = "/navigate_to_pose [nav2_msgs/action/NavigateToPose]\n"

    monkeypatch.setattr(module.bp, "ros2", lambda *a, **k: Done())
    assert module.action_server_ready({}, "/navigate_to_pose") is True


def test_action_server_ready_false_when_not_listed(monkeypatch):
    killed = []
    module = load_module(monkeypatch, killed)

    class Done:
        stdout = "/compute_path_to_pose [nav2_msgs/action/ComputePathToPose]\n"

    # One iteration only, so the test does not wait the full poll budget.
    monkeypatch.setattr(module.bp, "ros2", lambda *a, **k: Done())
    monkeypatch.setattr(module.time, "sleep", lambda s: None)
    assert module.action_server_ready({}, "/navigate_to_pose", steps=1) is False


def test_connect_goals_only_after_ready(monkeypatch):
    """The goal command is unreachable when a stage fails.

    `main` returns 1 from the failure path before reaching the
    `run_foreground` call; this pins the property at the level the reviewer
    asked about - a goal never runs against a stack that did not reach READY.
    """
    killed = []
    module = load_module(monkeypatch, killed)

    class Done:
        returncode = 0
        stdout = ""

    # Preconditions pass, but the daemon is "not ready" so the first stage
    # fails. run_foreground must never be called.
    monkeypatch.setattr(module.bp, "wait_bridge_ready", lambda env: 1)
    called = {"foreground": False}

    def fake_foreground(*a, **k):
        called["foreground"] = True
        return 0

    monkeypatch.setattr(module.bp, "run_foreground", fake_foreground)
    monkeypatch.setattr(module.bp, "environment", lambda: {})
    monkeypatch.setattr(module, "check_files", lambda *a, **k: True)
    monkeypatch.setattr(module.bp, "kill_nodes",
                        lambda names, note="": killed.append(list(names)) or False)
    code = module.main(["--goal", "1,2"])
    assert code == 1
    assert called["foreground"] is False
