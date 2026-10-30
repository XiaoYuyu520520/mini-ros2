// The configuration file the shell reads.
//
// JSON has no comment syntax, so a comment is a key whose name starts with
// `_comment`: `encoding/json` ignores keys it was not told about, so those
// entries are readable to a human and harmless to the program.
package main

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
)

// Config is one `config.json`. Everything is optional; the zero value is
// replaced by defaults in `loadConfig`.
type Config struct {
	Window WindowConfig           `json:"window"`
	Layout LayoutConfig           `json:"layout"`
	Views  map[string]ViewConfig  `json:"views"`
	Topics map[string]TopicConfig `json:"topics"`
	Tools  map[string]ToolConfig  `json:"tools"`
}

type WindowConfig struct {
	Width  int  `json:"width"`
	Height int  `json:"height"`
	Center bool `json:"center"`
}

// LayoutConfig is the dock: `grid` is rows of panel ids, `sizes` the relative
// width of each column. The shell only understands two ids - "view" and the
// side panel - but the shape is the one the full tool will use.
type LayoutConfig struct {
	Grid  [][]string `json:"grid"`
	Sizes []float64  `json:"sizes"`
	// PanelOffset is where the splitter sits, as a fraction of the width.
	PanelOffset float64 `json:"panel_offset"`
}

type ZoomConfig struct {
	Min float64 `json:"min"`
	Max float64 `json:"max"`
}

type ViewConfig struct {
	Kind       string     `json:"kind"`
	FixedFrame string     `json:"fixed_frame"`
	Background string     `json:"background"`
	GridCellM  float64    `json:"grid_cell_m"`
	Zoom       ZoomConfig `json:"zoom"`
	Pan        bool       `json:"pan"`
}

// TopicConfig is one display. `qos` is what the sidecar will hand the agent:
// it is the only place reliability and durability matter, because the shared
// memory channel itself has no QoS.
type TopicConfig struct {
	Type      string    `json:"type"`
	QOS       QOSConfig `json:"qos"`
	Color     string    `json:"color"`
	PointSize float64   `json:"point_size"`
	Width     float64   `json:"width"`
	Alpha     float64   `json:"alpha"`
	Enabled   *bool     `json:"enabled"`
}

type QOSConfig struct {
	Reliability string `json:"reliability"`
	Durability  string `json:"durability"`
	Depth       int    `json:"depth"`
}

type ToolConfig struct {
	Topic  string `json:"topic"`
	Action string `json:"action"`
	Type   string `json:"type"`
}

// defaultConfig is what the shell shows when there is no config.json yet.
// The topic list is deliberately the two the map view needs, so a fresh
// checkout has something to draw before the sidecar exists.
func defaultConfig() Config {
	cfg := Config{
		Window: WindowConfig{Width: 1440, Height: 900, Center: true},
		Layout: LayoutConfig{
			Grid:        [][]string{{"view", "panel"}},
			Sizes:       []float64{3, 1},
			PanelOffset: 0.74,
		},
		Views: map[string]ViewConfig{
			"view": {
				Kind:       "grid",
				FixedFrame: "map",
				Background: "#1e1e1e",
				GridCellM:  1.0,
				Zoom:       ZoomConfig{Min: 0.02, Max: 40},
				Pan:        true,
			},
		},
		Topics: map[string]TopicConfig{
			"/map":  {Type: "nav_msgs/msg/OccupancyGrid", Color: "#dddddd", Alpha: 0.7},
			"/scan": {Type: "sensor_msgs/msg/LaserScan", Color: "#00ff88", PointSize: 2},
			"/plan": {Type: "nav_msgs/msg/Path", Color: "#ff4444", Width: 2},
		},
		Tools: map[string]ToolConfig{
			"goal":         {Action: "navigate_to_pose", Type: "nav2_msgs/action/NavigateToPose"},
			"goal_topic":   {Topic: "/goal_pose", Type: "geometry_msgs/msg/PoseStamped"},
			"initial_pose": {Topic: "/initialpose", Type: "geometry_msgs/msg/PoseWithCovarianceStamped"},
		},
	}
	return cfg
}

// loadConfig reads `path`, filling anything missing from the defaults. A
// missing file is not an error: the shell starts with defaults and says so.
func loadConfig(path string) (Config, string, error) {
	cfg := defaultConfig()
	if path == "" {
		path = defaultConfigPath()
	}
	raw, err := os.ReadFile(path)
	if os.IsNotExist(err) {
		return cfg, path, nil
	}
	if err != nil {
		return cfg, path, err
	}
	if err := json.Unmarshal(raw, &cfg); err != nil {
		return cfg, path, fmt.Errorf("%s: %w", path, err)
	}
	return cfg, path, nil
}

// defaultConfigPath looks next to the executable first, then in the working
// directory, so a double-clicked exe finds its own config.
func defaultConfigPath() string {
	if exe, err := os.Executable(); err == nil {
		p := filepath.Join(filepath.Dir(exe), "config.json")
		if _, err := os.Stat(p); err == nil {
			return p
		}
	}
	return "config.json"
}

// enabled reports whether a display is on. Absent means on.
func (t TopicConfig) enabled() bool { return t.Enabled == nil || *t.Enabled }
