package main

import (
	"math"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"fyne.io/fyne/v2"
	"fyne.io/fyne/v2/test"
)

// Save must write what is on screen, not the file that was loaded: a resize or
// a splitter drag right before pressing Save is part of the configuration.
func TestShellSaveWritesCurrentLayout(t *testing.T) {
	a := test.NewApp()
	defer a.Quit()
	w := test.NewWindow(nil)

	cfg := defaultConfig()
	cfg.Layout.PanelOffset = 0.6
	cfg.Window.Width = 800
	cfg.Window.Height = 600

	path := filepath.Join(t.TempDir(), "config.json")
	seed := `{
  "_comment": "shell save must keep this",
  "window": {"width": 800, "height": 600},
  "layout": {"panel_offset": 0.6}
}`
	if err := os.WriteFile(path, []byte(seed), 0o644); err != nil {
		t.Fatal(err)
	}

	sh := buildUI(a, w, &cfg, path)

	w.Resize(fyne.NewSize(1234, 678))
	sh.setViewOffset(0.5)
	if err := sh.save(); err != nil {
		t.Fatalf("save: %v", err)
	}

	got, _, err := loadConfig(path)
	if err != nil {
		t.Fatalf("saved file does not load: %v", err)
	}
	if math.Abs(got.Layout.PanelOffset-0.5) > 1e-6 {
		t.Fatalf("panel offset not saved: got %v, want 0.5", got.Layout.PanelOffset)
	}
	if got.Window.Width != 1234 || got.Window.Height != 678 {
		t.Fatalf("window size not saved: got %dx%d, want 1234x678",
			got.Window.Width, got.Window.Height)
	}

	raw, err := os.ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(string(raw), "shell save must keep this") {
		t.Fatalf("comment lost on save:\n%s", raw)
	}

	// A collapsed panel must not be remembered as the layout: the next start
	// would open the window with the panel squeezed to the edge.
	sh.setViewOffset(0.5)
	sh.togglePanel()
	if !sh.collapsed {
		t.Fatal("panel did not collapse")
	}
	if err := sh.save(); err != nil {
		t.Fatalf("save while collapsed: %v", err)
	}
	collapsedCfg, _, err := loadConfig(path)
	if err != nil {
		t.Fatal(err)
	}
	if math.Abs(collapsedCfg.Layout.PanelOffset-0.5) > 1e-6 {
		t.Fatalf("collapsed offset was saved: got %v, want 0.5",
			collapsedCfg.Layout.PanelOffset)
	}
}

// Load has to be visible: the window size and the splitter position in the file
// are applied, not just the topic list.
func TestShellReloadAppliesLayout(t *testing.T) {
	a := test.NewApp()
	defer a.Quit()
	w := test.NewWindow(nil)

	cfg := defaultConfig()
	path := filepath.Join(t.TempDir(), "config.json")
	if err := os.WriteFile(path, []byte(`{
  "window": {"width": 1000, "height": 640},
  "layout": {"panel_offset": 0.3},
  "topics": {"/only": {"type": "std_msgs/msg/String"}}
}`), 0o644); err != nil {
		t.Fatal(err)
	}

	sh := buildUI(a, w, &cfg, path)
	if err := sh.reload(); err != nil {
		t.Fatalf("reload: %v", err)
	}

	if math.Abs(sh.viewOffset()-0.3) > 1e-6 {
		t.Fatalf("splitter not applied: got %v, want 0.3", sh.viewOffset())
	}
	size := w.Canvas().Size()
	if int(size.Width) != 1000 || int(size.Height) != 640 {
		t.Fatalf("window size not applied: got %vx%v, want 1000x640",
			size.Width, size.Height)
	}
	if len(sh.topicNames) != 1 || sh.topicNames[0] != "/only" {
		t.Fatalf("topic list not replaced: %v", sh.topicNames)
	}
}
