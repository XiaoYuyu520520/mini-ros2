package main

import (
	"math"
	"os"
	"path/filepath"
	"testing"

	"fyne.io/fyne/v2"
	"fyne.io/fyne/v2/test"
)

// The panel must collapse to the edge and come back to exactly where it was.
// This is the layout behaviour the shell exists to prove; driving it through
// the real toggle keeps the test honest about what a click does.
//
// The panel is on the left, so the split offset is the panel's share of the
// window and the viewport's share is what the configuration records.
func TestPanelCollapseAndRestore(t *testing.T) {
	a := test.NewApp()
	defer a.Quit()
	w := test.NewWindow(nil)

	cfg := defaultConfig()
	sh := buildUI(a, w, &cfg, "test")
	if sh == nil || sh.split == nil {
		t.Fatal("buildUI returned no split")
	}

	open := sh.viewOffset()
	if open <= 0 || open >= 0.9 {
		t.Fatalf("unexpected initial viewport share %v", open)
	}
	// The panel is on the left: it takes the smaller share.
	if sh.split.Offset >= 0.5 {
		t.Fatalf("the panel is not on the left: offset %v", sh.split.Offset)
	}

	sh.togglePanel()
	if !sh.collapsed {
		t.Fatal("collapsed flag not set")
	}
	if sh.viewOffset() < 0.9 {
		t.Fatalf("panel did not fold to a rail: viewport share %v", sh.viewOffset())
	}

	sh.togglePanel()
	if sh.collapsed {
		t.Fatal("collapsed flag not cleared")
	}
	if sh.viewOffset() != open {
		t.Fatalf("panel did not return to %v (got %v)", open, sh.viewOffset())
	}
}

// `_comment` keys are the file's comment mechanism and must be ignored, while
// real values must override the defaults.
func TestConfigCommentsAndOverrides(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "config.json")
	body := `{
	  "_comment": "this must not be an error",
	  "_comment_topics": "and neither must this",
	  "window": {"width": 1234, "height": 567},
	  "topics": {
	    "/scan": {
	      "_comment": "best_effort matters on the DDS side",
	      "type": "sensor_msgs/msg/LaserScan",
	      "qos": {"reliability": "best_effort", "depth": 5}
	    }
	  }
	}`
	if err := os.WriteFile(path, []byte(body), 0o644); err != nil {
		t.Fatal(err)
	}

	cfg, got, err := loadConfig(path)
	if err != nil {
		t.Fatalf("loadConfig: %v", err)
	}
	if got != path {
		t.Fatalf("path %q, want %q", got, path)
	}
	if cfg.Window.Width != 1234 || cfg.Window.Height != 567 {
		t.Fatalf("window override lost: %+v", cfg.Window)
	}
	scan, ok := cfg.Topics["/scan"]
	if !ok {
		t.Fatal("/scan missing")
	}
	if scan.QOS.Reliability != "best_effort" || scan.QOS.Depth != 5 {
		t.Fatalf("qos lost: %+v", scan.QOS)
	}
	// `topics` is a section the file names, so it replaces the defaults rather
	// than merging with them: /map is not silently shown when the user listed
	// only /scan.
	if len(cfg.Topics) != 1 {
		t.Fatalf("topics should be exactly the file's list, got %v", cfg.Topics)
	}
	if _, ok := cfg.Topics["/map"]; ok {
		t.Fatal("default /map leaked into a file that named its own topics")
	}
}

// A missing config file is not a failure: the shell has to start with the
// defaults, which is what a fresh checkout does.
func TestMissingConfigUsesDefaults(t *testing.T) {
	cfg, _, err := loadConfig(filepath.Join(t.TempDir(), "nope.json"))
	if err != nil {
		t.Fatalf("missing file should not error: %v", err)
	}
	if cfg.Window.Width == 0 || len(cfg.Topics) == 0 {
		t.Fatalf("defaults not applied: %+v", cfg)
	}
}

// The viewport keeps the world point under the pointer fixed while zooming.
// This is the one bit of maths a user notices immediately if it is wrong.
func TestZoomKeepsPointerAnchored(t *testing.T) {
	a := test.NewApp()
	defer a.Quit()
	cfg := defaultConfig()
	v := newViewport(&cfg)
	v.Resize(fyne.NewSize(800, 600))

	pos := fyne.NewPos(600, 150)
	worldXBefore, worldYBefore := v.worldAt(pos)
	v.zoomAt(pos, 1.5)
	worldXAfter, worldYAfter := v.worldAt(pos)

	const eps = 1e-9
	if math.Abs(worldXBefore-worldXAfter) > eps || math.Abs(worldYBefore-worldYAfter) > eps {
		t.Fatalf("zoom moved the world under the pointer: (%v,%v) -> (%v,%v)",
			worldXBefore, worldYBefore, worldXAfter, worldYAfter)
	}
}
