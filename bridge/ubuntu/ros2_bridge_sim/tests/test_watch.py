#!/usr/bin/env python3
"""Tests for the ``--subscribe`` probe hook in :mod:`ros2_bridge_sim.run_sim`.

The bridge is verified in both directions by pushing a topic from Windows and
watching it land on the robot, which needs something on this side that
receives it: ``--subscribe TOPIC[:TYPE]`` registers exactly that. These tests
pin down the two behaviours an acceptance run depends on -- a known type is
decoded into the log, and a type this side has no schema for still shows up
(with its byte count) instead of being silently dropped.

Run under pytest or standalone::

    python tests/test_watch.py
"""

from __future__ import annotations

import io
import os
import sys
from contextlib import redirect_stdout

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

from ros2_bridge_sim import messages as m  # noqa: E402
from ros2_bridge_sim.graph import Graph  # noqa: E402
from ros2_bridge_sim.run_sim import (  # noqa: E402
    attach_probe_subscriptions,
    parse_subscription,
    summarize,
)

KNOWN = "nav2_msgs/msg/TrackingFeedback"
HEADER = {"stamp": {"sec": 5, "nanosec": 0}, "frame_id": "odom"}
TRACKING_FEEDBACK = {
    "header": HEADER,
    "position_tracking_error": 0.25,
    "heading_tracking_error": 0.0,
    "current_path_index": 3,
    "robot_pose": {
        "header": HEADER,
        "pose": {
            "position": {"x": 1.0, "y": 2.0, "z": 0.0},
            "orientation": {"x": 0.0, "y": 0.0, "z": 0.0, "w": 1.0},
        },
    },
    "distance_to_goal": 4.5,
    "speed": 0.4,
    "remaining_path_length": 6.0,
}
STRING_BYTES = m.encode({"data": "hello"}, "std_msgs/msg/String")


def watch(specs, type_name=KNOWN, data=None, topic="/probe/x"):
    """Attach the probes, publish one message, return what was logged."""
    graph = Graph()
    handles = attach_probe_subscriptions(graph, specs)
    payload = data if data is not None else m.encode(TRACKING_FEEDBACK, KNOWN)
    buffer = io.StringIO()
    with redirect_stdout(buffer):
        graph.publish(topic, type_name, payload)
    return handles, buffer.getvalue()


# --------------------------------------------------------------------------
# Argument parsing
# --------------------------------------------------------------------------

def test_parse_subscription_splits_topic_and_type():
    assert parse_subscription("/probe/x:" + KNOWN) == ("/probe/x", KNOWN)
    assert parse_subscription(" /probe/x : " + KNOWN + " ") == ("/probe/x", KNOWN)


def test_parse_subscription_without_type():
    assert parse_subscription("/probe/x") == ("/probe/x", "")


def test_parse_subscription_rejects_empty_topic():
    for bad in ("", ":", "  ", ":" + KNOWN):
        try:
            parse_subscription(bad)
        except ValueError:
            continue
        raise AssertionError("accepted %r" % bad)


def test_summarize_truncates_long_messages():
    assert summarize({"a": 1}) == '{"a": 1}'
    long = summarize({"a": "x" * 400})
    assert len(long) == 240 and long.endswith("...")


# --------------------------------------------------------------------------
# The hook itself
# --------------------------------------------------------------------------

def test_probe_decodes_a_known_type():
    handles, logged = watch(["/probe/x:" + KNOWN])
    assert len(handles) == 1
    assert "[sub] /probe/x #1 " + KNOWN + " <- " in logged
    assert '"current_path_index": 3' in logged
    assert '"distance_to_goal": 4.5' in logged


def test_probe_reports_an_unknown_type_instead_of_dropping_it():
    # String bytes announced as a nav2 type: the probe must say so, not vanish.
    handles, logged = watch(["/probe/x"], type_name="nav2_msgs/msg/NotARealType", data=STRING_BYTES)
    assert len(handles) == 1
    assert "no schema for nav2_msgs/msg/NotARealType" in logged
    assert "%d byte(s)" % len(STRING_BYTES) in logged


def test_probe_registers_the_topic_even_before_anything_is_published():
    graph = Graph()
    attach_probe_subscriptions(graph, ["/probe/idle:std_msgs/msg/Bool"])
    topic = graph.get_topic("/probe/idle")
    assert topic is not None
    assert topic.type == "std_msgs/msg/Bool"
    assert topic.subscriber_count == 1
    assert topic.publisher_count == 0


def test_probe_counts_every_message():
    graph = Graph()
    attach_probe_subscriptions(graph, ["/probe/x:" + KNOWN])
    buffer = io.StringIO()
    with redirect_stdout(buffer):
        for _ in range(3):
            graph.publish("/probe/x", "std_msgs/msg/String", STRING_BYTES)
    logged = buffer.getvalue()
    assert "#1 " in logged and "#2 " in logged and "#3 " in logged


def test_no_specs_means_no_subscriptions():
    graph = Graph()
    assert attach_probe_subscriptions(graph, []) == []
    assert graph.topics() == []
