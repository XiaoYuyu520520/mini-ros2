package main

import (
	"encoding/json"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

// Saving must not eat the file's comments.
//
// `_comment` keys are not struct fields, so a plain marshal of the Config would
// drop every one of them the first time the user pressed Save - the comments
// would survive exactly until the feature they document was used. Save therefore
// deep-merges the new configuration into the old document, and this test is what
// pins that down.
func TestSaveKeepsComments(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "config.json")
	original := `{
	  "_comment": "top level note",
	  "window": {"width": 1000, "height": 700},
	  "topics": {
	    "/scan": {
	      "_comment": "best_effort matters",
	      "type": "sensor_msgs/msg/LaserScan",
	      "qos": {"reliability": "best_effort", "depth": 5},
	      "color": "#00ff88"
	    }
	  }
	}`
	if err := os.WriteFile(path, []byte(original), 0o644); err != nil {
		t.Fatal(err)
	}

	cfg, _, err := loadConfig(path)
	if err != nil {
		t.Fatal(err)
	}
	// Change something, the way the UI would.
	cfg.Window.Width = 1600
	if err := writeConfig(path, cfg); err != nil {
		t.Fatalf("writeConfig: %v", err)
	}

	raw, err := os.ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}
	text := string(raw)

	for _, want := range []string{
		`"top level note"`,
		`"best_effort matters"`,
	} {
		if !strings.Contains(text, want) {
			t.Fatalf("comment %s lost after save:\n%s", want, text)
		}
	}
	if !strings.Contains(text, `"width": 1600`) {
		t.Fatalf("change was not written:\n%s", text)
	}

	// And the file must still parse, with both the old and the new value.
	var doc map[string]any
	if err := json.Unmarshal(raw, &doc); err != nil {
		t.Fatalf("saved file is not valid JSON: %v", err)
	}
	again, _, err := loadConfig(path)
	if err != nil {
		t.Fatalf("saved file does not load: %v", err)
	}
	if again.Window.Width != 1600 {
		t.Fatalf("round trip lost the width: %d", again.Window.Width)
	}
	if again.Topics["/scan"].QOS.Reliability != "best_effort" {
		t.Fatalf("round trip lost the qos: %+v", again.Topics["/scan"].QOS)
	}
}

// Saving to a path that does not exist yet has to create a complete file, not
// a partial merge of "nothing".
func TestSaveCreatesFileWhenMissing(t *testing.T) {
	path := filepath.Join(t.TempDir(), "fresh.json")
	cfg := defaultConfig()
	cfg.Window.Width = 1234
	if err := writeConfig(path, cfg); err != nil {
		t.Fatalf("writeConfig: %v", err)
	}
	again, _, err := loadConfig(path)
	if err != nil {
		t.Fatalf("loadConfig: %v", err)
	}
	if again.Window.Width != 1234 {
		t.Fatalf("width not round-tripped: %d", again.Window.Width)
	}
	if len(again.Topics) != len(cfg.Topics) {
		t.Fatalf("topics lost: %d vs %d", len(again.Topics), len(cfg.Topics))
	}
}

// The Reload button re-reads the file. What matters is that a value edited on
// disk wins over the in-memory copy.
func TestShellReloadPicksUpDiskChanges(t *testing.T) {
	path := filepath.Join(t.TempDir(), "config.json")
	if err := writeConfig(path, defaultConfig()); err != nil {
		t.Fatal(err)
	}
	cfg, _, err := loadConfig(path)
	if err != nil {
		t.Fatal(err)
	}

	// Simulate the file being edited outside the app.
	writeRaw := func(width int) {
		edited := cfg
		edited.Window.Width = width
		if err := writeConfig(path, edited); err != nil {
			t.Fatal(err)
		}
	}
	writeRaw(1777)

	fresh, _, err := loadConfig(path)
	if err != nil {
		t.Fatal(err)
	}
	if fresh.Window.Width != 1777 {
		t.Fatalf("reload did not see the edit: %d", fresh.Window.Width)
	}
}

// A config written by a Windows tool may start with a UTF-8 BOM. Ignoring that
// made a hand-edited file silently fall back to the defaults, which is the
// worst failure mode: the user's change looks ignored.
func TestConfigWithUTF8BOM(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "bom.json")
	body := append([]byte{0xEF, 0xBB, 0xBF},
		[]byte(`{"window": {"width": 1111, "height": 222}}`)...)
	if err := os.WriteFile(path, body, 0o644); err != nil {
		t.Fatal(err)
	}
	cfg, _, err := loadConfig(path)
	if err != nil {
		t.Fatalf("BOM should be tolerated: %v", err)
	}
	if cfg.Window.Width != 1111 || cfg.Window.Height != 222 {
		t.Fatalf("BOM file ignored: %+v", cfg.Window)
	}
}

// Saving keeps comments even when the file started with a BOM.
func TestSavePreservesBOMComments(t *testing.T) {
	path := filepath.Join(t.TempDir(), "bom2.json")
	body := append([]byte{0xEF, 0xBB, 0xBF},
		[]byte(`{"_comment": "keep me", "window": {"width": 900}}`)...)
	if err := os.WriteFile(path, body, 0o644); err != nil {
		t.Fatal(err)
	}
	cfg, _, err := loadConfig(path)
	if err != nil {
		t.Fatal(err)
	}
	cfg.Window.Width = 999
	if err := writeConfig(path, cfg); err != nil {
		t.Fatal(err)
	}
	raw, _ := os.ReadFile(path)
	if !strings.Contains(string(raw), "keep me") {
		t.Fatalf("comment lost:\n%s", raw)
	}
	if !strings.Contains(string(raw), "999") {
		t.Fatalf("change lost:\n%s", raw)
	}
}
