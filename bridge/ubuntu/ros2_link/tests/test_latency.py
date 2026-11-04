# -*- coding: utf-8 -*-
"""Add a test for the cached percentiles.

The relay's snapshot asks every topic for its distribution, and sorting a
reservoir of 512 floats for each of a hundred topics holds the relay's lock
for milliseconds. The percentiles are therefore kept ready: the sorted copy
is rebuilt when a sample arrives, not when somebody asks.

The test pins down what matters: the numbers are the same as they were when
they were computed on demand, and they stay correct when the reservoir
wraps around (which is where a cached copy could go wrong - the sample that
falls off the ring is the one the sorted copy must not keep).
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

from ros2_link.relay import LATENCY_MAX_SAMPLES, _Latency  # noqa: E402


def percentile_of(samples: list[float], fraction: float) -> float:
    """The percentile the old, on-demand implementation would have reported."""
    if not samples:
        return 0.0
    ordered = sorted(samples)
    index = min(len(ordered) - 1, int(len(ordered) * fraction))
    return round(ordered[index], 3)


def test_percentiles_match_the_on_demand_ones() -> None:
    tracker = _Latency()
    now = time.time_ns()
    for i in range(200):
        tracker.add(now, now - i * 1_000_000)
    snapshot = tracker.snapshot()
    expected = [tracker.samples[i] for i in range(len(tracker.samples))]
    assert snapshot["samples"] == len(expected), snapshot
    for fraction, key in ((0.50, "p50_ms"), (0.95, "p95_ms"), (0.99, "p99_ms")):
        assert snapshot[key] == percentile_of(expected, fraction), (key, snapshot)
    assert snapshot["max_ms"] == round(max(expected), 3), snapshot


def test_the_percentiles_survive_the_reservoir_wrapping() -> None:
    """More samples than the reservoir holds.

    The reservoir is a ring: the oldest samples fall off it. A cached sorted
    copy that forgets to drop them would report a percentile of a sample
    that is no longer there.
    """
    tracker = _Latency()
    now = time.time_ns()
    total = LATENCY_MAX_SAMPLES * 3
    for i in range(total):
        # Every sample is older than the one before it, so the oldest is the
        # smallest and the newest the largest.
        tracker.add(now, now - (total - i) * 1_000_000)
        if i % 97 == 0:
            tracker.snapshot()  # keep the cache in step with the arrivals

    snapshot = tracker.snapshot()
    expected = list(tracker.samples)
    assert snapshot["samples"] == len(expected) == LATENCY_MAX_SAMPLES, snapshot
    assert snapshot["max_ms"] == round(max(expected), 3), snapshot
    for fraction, key in ((0.50, "p50_ms"), (0.95, "p95_ms"), (0.99, "p99_ms")):
        assert snapshot[key] == percentile_of(expected, fraction), (key, snapshot)


def test_an_unusable_stamp_is_counted_and_not_sampled() -> None:
    tracker = _Latency()
    now = time.time_ns()
    assert tracker.add(now, 0) is False, "a stamp of zero is not a latency"
    assert tracker.add(now, now + 60 * 10**9) is False, "a future stamp is not either"
    snapshot = tracker.snapshot()
    assert snapshot["samples"] == 0, snapshot
    assert snapshot["stale_stamps"] == 2, snapshot


if __name__ == "__main__":
    test_percentiles_match_the_on_demand_ones()
    test_the_percentiles_survive_the_reservoir_wrapping()
    test_an_unusable_stamp_is_counted_and_not_sampled()
    print("PASS")
