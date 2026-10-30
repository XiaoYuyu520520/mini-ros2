# -*- coding: utf-8 -*-
"""Walk `BasicNavigator`'s *result* face, not just its action face.

`04-gaps.md` 第二十八节第 3 条记的是：action 面（构造、`send_goal_async`、`accepted`）
实测过，结果面没跑过 —— `isTaskComplete` / `getFeedback` / `getResult` / `cancelTask` /
`clearAllCostmaps` / `waitUntilNav2Active` / `lifecycleShutdown`。这个探针把它们一条一条
点过去，每步一行 `PROBE`，最后一张表。

三个刻意的设计：

* 每一步先看服务在不在，不在就记 SKIP —— `wait_for_service()` 和 `_waitForInitialPose()`
  都是无限循环，探针不该被它们挂住；
* 会阻塞而又没有超时参数的调用（`waitUntilNav2Active`、`lifecycleShutdown`）放到线程里
  跑，主线程 `join(超时)`。超时就记 FAIL 并结束；
* `waitUntilNav2Active` 之前先 A/B 一次 `/initialpose`：`BasicNavigator.initial_pose` 是
  默认构造的 `PoseStamped`，四元数是 (0,0,0,0)，而 `nav2::validateMsg` 要求
  `|‖q‖-1| < 1e-4`。默认那个只发一次（不然 `_waitForInitialPose` 的紧循环会写几 MB
  日志），看它能不能让 `initial_pose_received` 翻过来。
"""
import sys
import threading
import time

import rclpy
from geometry_msgs.msg import PoseStamped

TARGET_1 = (2.0, -1.0, -0.1601822430069672, 0.9870874576374967)
TARGET_2 = (3.5, -1.5, -0.5847102846637650, 0.8112421851755608)

results = []


def record(name, ok, detail=""):
    results.append((name, ok, detail))
    print("PROBE %-30s %s  %s" % (name, "PASS" if ok else "FAIL", detail), flush=True)


def skip(name, detail):
    results.append((name, None, detail))
    print("PROBE %-30s SKIP  %s" % (name, detail), flush=True)


def note(name, detail):
    results.append((name, None, detail))
    print("PROBE %-30s NOTE  %s" % (name, detail), flush=True)


def guarded(name, fn, timeout):
    """Call `fn` on a thread; return True if it finished in time."""
    out = {}

    def body():
        start = time.monotonic()
        try:
            fn()
        except Exception as error:  # noqa: BLE001 - the point is to report it
            out["ok"] = False
            out["detail"] = "%s: %s" % (type(error).__name__, error)
        else:
            out["ok"] = True
            out["detail"] = "%.1f s" % (time.monotonic() - start)

    thread = threading.Thread(target=body, daemon=True)
    thread.start()
    thread.join(timeout)
    if thread.is_alive():
        record(name, False, "did not return within %.0f s" % timeout)
        return False
    record(name, out.get("ok", False), out.get("detail", ""))
    return out.get("ok", False)


def pose(x, y, z, w):
    p = PoseStamped()
    p.header.frame_id = "map"
    p.pose.position.x = float(x)
    p.pose.position.y = float(y)
    p.pose.orientation.z = float(z)
    p.pose.orientation.w = float(w)
    return p


def spin_for(node, seconds):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        rclpy.spin_once(node, timeout_sec=0.02)


def main():
    rclpy.init()
    from nav2_simple_commander.robot_navigator import BasicNavigator, TaskResult

    start = time.monotonic()
    nav = BasicNavigator()

    # `get_service_names_and_types()` answers fully qualified names; the ported
    # `BasicNavigator` asks for them without the leading slash.
    table = {name.lstrip("/"): types for name, types in nav.get_service_names_and_types()}
    topics = {name.lstrip("/"): types for name, types in nav.get_topic_names_and_types()}
    print("PROBE the node sees %d service(s), %d topic(s)" % (len(table), len(topics)),
          flush=True)

    needed_services = {
        "waitUntilNav2Active": ("amcl/get_state", "bt_navigator/get_state"),
        "clearAllCostmaps": ("local_costmap/clear_entirely_local_costmap",
                            "global_costmap/clear_entirely_global_costmap"),
        "lifecycleShutdown": ("lifecycle_manager/manage_nodes",),
    }
    for step, names in needed_services.items():
        for name in names:
            print("PROBE for %-18s %-48s %s" % (step, name, table.get(name, "MISSING")),
                  flush=True)
    for name in ("amcl_pose", "initialpose", "navigate_to_pose/_action/status", "cmd_vel"):
        print("PROBE topic %-48s %s" % (name, topics.get(name, "MISSING")), flush=True)

    # --- A/B on the initial pose -------------------------------------------
    nav.initial_pose_pub.publish(nav.initial_pose)   # the malformed default, once
    spin_for(nav, 1.5)
    note("default initial_pose", "accepted=%s (the class' own default, quaternion 0,0,0,0)"
         % nav.initial_pose_received)
    if nav.initial_pose_received:
        record("setInitialPose", False, "the zero quaternion was accepted after all")
    else:
        nav.setInitialPose(pose(0.0, 0.0, 0.0, 1.0))
        spin_for(nav, 1.5)
        record("setInitialPose", nav.initial_pose_received,
               "pose with w=1 -> amcl_pose arrived=%s" % nav.initial_pose_received)

    # --- waitUntilNav2Active (the default amcl path) ------------------------
    if all(name in table for name in needed_services["waitUntilNav2Active"]) \
            and "amcl_pose" in topics:
        if not guarded("waitUntilNav2Active", nav.waitUntilNav2Active, 90.0):
            print("PROBE aborting: the navigator is wedged", flush=True)
            return report(start)
    else:
        skip("waitUntilNav2Active", "amcl/get_state or bt_navigator/get_state is missing")

    # --- goToPose + isTaskComplete + getFeedback + getResult ----------------
    print("PROBE goToPose %s" % (TARGET_1,), flush=True)
    t0 = time.monotonic()
    nav.goToPose(pose(*TARGET_1))
    first = None
    last = None
    polls = 0
    while not nav.isTaskComplete():
        polls += 1
        feedback = nav.getFeedback()
        if feedback is not None:
            if first is None:
                first = feedback.distance_remaining
            last = feedback.distance_remaining
        time.sleep(0.2)
    result = nav.getResult()
    record("goToPose(1)", result == TaskResult.SUCCEEDED,
           "%s in %.1f s, %d polls" % (result, time.monotonic() - t0, polls))
    record("getFeedback", first is not None,
           "first %.2f m -> last %.2f m" % (first if first is not None else -1,
                                            last if last is not None else -1))
    record("getResult", result is not None, str(result))
    print("PROBE task error %r" % (nav.getTaskError(),), flush=True)

    # --- cancelTask ---------------------------------------------------------
    print("PROBE goToPose %s then cancelTask" % (TARGET_2,), flush=True)
    nav.goToPose(pose(*TARGET_2))
    t0 = time.monotonic()
    while time.monotonic() - t0 < 6.0 and not nav.isTaskComplete():
        time.sleep(0.2)
    t_cancel = time.monotonic()
    nav.cancelTask()
    while not nav.isTaskComplete() and time.monotonic() - t_cancel < 20.0:
        time.sleep(0.2)
    result = nav.getResult()
    record("cancelTask", result == TaskResult.CANCELED,
           "%s %.1f s after the cancel" % (result, time.monotonic() - t_cancel))
    print("PROBE task error %r" % (nav.getTaskError(),), flush=True)

    # --- clearAllCostmaps ---------------------------------------------------
    if all(name in table for name in needed_services["clearAllCostmaps"]):
        guarded("clearAllCostmaps", nav.clearAllCostmaps, 30.0)
    else:
        skip("clearAllCostmaps", "one of the two clear services is not in the graph")

    # --- lifecycleShutdown (last: it stops the servers) ---------------------
    if all(name in table for name in needed_services["lifecycleShutdown"]):
        guarded("lifecycleShutdown", nav.lifecycleShutdown, 90.0)
    else:
        skip("lifecycleShutdown", "lifecycle_manager/manage_nodes is not in the graph")

    return report(start)


def report(start):
    passed = sum(1 for _, ok, _ in results if ok is True)
    failed = sum(1 for _, ok, _ in results if ok is False)
    skipped = sum(1 for _, ok, _ in results if ok is None)
    print("\nPROBE total %d step(s): %d pass, %d fail, %d other, %.1f s"
          % (len(results), passed, failed, skipped, time.monotonic() - start), flush=True)
    return 1 if failed else 0


sys.exit(main())
