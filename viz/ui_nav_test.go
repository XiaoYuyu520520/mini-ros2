package main

import "testing"

// The navigation panel reads two numbers out of the feedback text the sidecar
// sends. That text is a human readable dump of the action's feedback message,
// not JSON, so the numbers are matched by name; these cases pin down that the
// match is on the right field and that a missing field is reported as missing
// rather than as a zero that looks like a measurement.
func TestFeedbackDistanceAndTime(t *testing.T) {
	text := `current_pose:
  header:
    frame_id: map
  pose:
    position:
      x: 0.5
      y: 0.25
navigation_time:
  sec: 3
  nanosec: 0
estimated_time_remaining:
  sec: 2
  nanosec: 500000000
number_of_recoveries: 0
distance_remaining: 1.42
`
	// navigation_time and estimated_time_remaining are Durations, so the time
	// is in whole seconds here; the panel still picks the number out.
	if value, ok := feedbackDistance(text); !ok || value != 1.42 {
		t.Fatalf("distance: got %v ok=%v, want 1.42", value, ok)
	}
	// The duration prints as two integers here; that is not a single number
	// the panel should show, so it stays hidden rather than wrong.
	if _, ok := feedbackTime(text); ok {
		t.Fatalf("time: a two-integer duration must not be read as one number")
	}

	plain := "distance_remaining: 3.5\nestimated_time_remaining: 7.25\n"
	if value, ok := feedbackDistance(plain); !ok || value != 3.5 {
		t.Fatalf("plain distance: got %v ok=%v, want 3.5", value, ok)
	}
	if value, ok := feedbackTime(plain); !ok || value != 7.25 {
		t.Fatalf("plain time: got %v ok=%v, want 7.25", value, ok)
	}

	if _, ok := feedbackDistance("no such field here"); ok {
		t.Fatal("a missing distance must not be reported")
	}
	if _, ok := feedbackTime(""); ok {
		t.Fatal("a missing time must not be reported")
	}
}

// stateText has to cover every state the sidecar reports with a non-empty
// label, so the panel never shows a bare internal word.
func TestNavStateText(t *testing.T) {
	for _, state := range []string{
		"", "sending", "sent", "accepted", "running", "succeeded",
		"aborted", "canceled", "rejected", "error", "stopped",
	} {
		if stateText(state) == "" {
			t.Fatalf("stateText(%q) is empty", state)
		}
	}
	if stateText("succeeded") == "succeeded" {
		t.Fatal("stateText should translate a known state to a human label")
	}
}

// goalAction reads the configured action and falls back to nav2's default.
func TestGoalActionDefaults(t *testing.T) {
	cfg := defaultConfig()
	sh := &shell{cfg: cfg}
	action, typeName, frame := sh.goalAction()
	if action != "/navigate_to_pose" {
		t.Fatalf("action: %q", action)
	}
	if typeName != "nav2_msgs/action/NavigateToPose" {
		t.Fatalf("type: %q", typeName)
	}
	if frame != "map" {
		t.Fatalf("frame: %q", frame)
	}
}
