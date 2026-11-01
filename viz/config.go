// The configuration file the shell reads.
//
// JSON has no comment syntax, so a comment is a key whose name starts with
// `_comment`: `encoding/json` ignores keys it was not told about, so those
// entries are readable to a human and harmless to the program.
package main

import (
	"bytes"
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
)

// Config is one `config.json`. Everything is optional; the zero value is
// replaced by defaults in `loadConfig`.
type Config struct {
	Window    WindowConfig           `json:"window"`
	UI        UIConfig               `json:"ui"`
	Layout    LayoutConfig           `json:"layout"`
	Views     map[string]ViewConfig  `json:"views"`
	Topics    map[string]TopicConfig `json:"topics"`
	Tools     map[string]ToolConfig  `json:"tools"`
	Display   DisplayConfig          `json:"display"`
	Transport TransportConfig        `json:"transport"`
}

// DisplayConfig is how a topic *type* is drawn. It is kept out of TopicConfig
// so the topic list stays a list of topics: a new /scan entry picks up the
// LaserScan style without anybody repeating the colour.
type DisplayConfig struct {
	OccupancyGrid OccupancyGridStyle `json:"occupancy_grid"`
	LaserScan     LaserScanStyle     `json:"laser_scan"`
	Path          PathStyle          `json:"path"`
	Pose          PoseStyle          `json:"pose"`
	Costmap       CostmapStyle       `json:"costmap"`
	PointCloud    PointCloudStyle    `json:"point_cloud"`
	TF            TFStyle            `json:"tf"`
}

type OccupancyGridStyle struct {
	FreeColor    string  `json:"free_color"`
	Occupied     string  `json:"occupied_color"`
	UnknownColor string  `json:"unknown_color"`
	Alpha        float64 `json:"alpha"`
}

type LaserScanStyle struct {
	Color string  `json:"color"`
	Size  float64 `json:"size"`
}

type PathStyle struct {
	Color string  `json:"color"`
	Width float64 `json:"width"`
}

type PoseStyle struct {
	Color      string  `json:"color"`
	Size       float64 `json:"size"`
	Covariance bool    `json:"covariance"`
}

// CostmapStyle is the palette for nav2 costmaps. The cells are costs 0..255:
// 0 is free (and stays transparent so the map below shows through), 1..100
// fade from LowColor to HighColor, 254 is an inscribed obstacle and 255 a
// lethal one.
type CostmapStyle struct {
	LowColor       string  `json:"low_color"`
	HighColor      string  `json:"high_color"`
	InscribedColor string  `json:"inscribed_color"`
	LethalColor    string  `json:"lethal_color"`
	UnknownColor   string  `json:"unknown_color"`
	Alpha          float64 `json:"alpha"`
}

// PointCloudStyle is how a PointCloud2 is drawn. ColorByHeight makes a
// rainbow from MinZ to MaxZ (0 means "use the cloud's own range"); when it is
// false the flat Color is used.
type PointCloudStyle struct {
	Color         string  `json:"color"`
	Size          float64 `json:"size"`
	MaxRange      float64 `json:"max_range"`
	ColorByHeight bool    `json:"color_by_height"`
	MinZ          float64 `json:"min_z"`
	MaxZ          float64 `json:"max_z"`
}

// TFStyle is the one thing drawn on the /tf topic: it is a live transform
// stream, not something you plot.
type TFStyle struct {
	ShowAxes bool `json:"show_axes"`
}

// TransportConfig says how the sidecar is reached. It is a child process over
// stdin/stdout, not a TCP port or a named pipe: the lifetime is tied to the
// viewer, there is nothing to firewall, and a crash cannot leave a listener
// behind. `cmd` names the executable when it is not next to viz.exe.
// `namespace` is exported as NAV2_SHM_NAMESPACE to the child; leave it empty
// to inherit the shell's environment.
type TransportConfig struct {
	Cmd       string `json:"cmd"`
	Namespace string `json:"namespace"`
}

// UIConfig is the language of the window. "zh" is the default; "en" keeps
// the original labels.
type UIConfig struct {
	Language string `json:"language"`
	// AddMode is which half of the Add dialog opens first: "topic" (the
	// default) or "type".
	AddMode string `json:"add_mode"`
	// SelectedDisplay is the display whose properties were open when the
	// window last closed.
	SelectedDisplay string `json:"selected_display"`
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
	// CenterX/CenterY/Scale remember where the view was looking and how far
	// in it was zoomed, so a restart opens on the same spot.
	CenterX float64 `json:"center_x,omitempty"`
	CenterY float64 `json:"center_y,omitempty"`
	Scale   float64 `json:"scale,omitempty"`
}

// TopicConfig is one topic entry. `qos` is what the sidecar will hand the
// agent: it is the only place reliability and durability matter, because the
// shared memory channel itself has no QoS. The colour/size/alpha fields are
// per-topic overrides of the type style in DisplayConfig; leaving them out
// means "use the type style", which is why a new /scan entry needs no
// decoration.
type TopicConfig struct {
	Type      string    `json:"type"`
	QOS       QOSConfig `json:"qos,omitzero"`
	Color     string    `json:"color,omitempty"`
	PointSize float64   `json:"point_size,omitempty"`
	Width     float64   `json:"width,omitempty"`
	Alpha     float64   `json:"alpha,omitempty"`
	// History keeps that many past messages for a scan or a cloud, so a
	// spinning lidar draws a trail the way rviz's History Length does.
	History int   `json:"history,omitempty"`
	Enabled *bool `json:"enabled,omitempty"`
}

type QOSConfig struct {
	Reliability string `json:"reliability,omitempty"`
	Durability  string `json:"durability,omitempty"`
	Depth       int    `json:"depth,omitempty"`
}

type ToolConfig struct {
	Topic  string `json:"topic"`
	Action string `json:"action"`
	Type   string `json:"type"`
	// Frame is the frame the tool publishes in. It is deliberately not the
	// view's fixed frame: a goal is a map-frame pose even when the user is
	// looking at the robot in odom.
	Frame string `json:"frame"`
}

// defaultConfig is what the shell shows when there is no config.json yet.
// The topic list is deliberately the two the map view needs, so a fresh
// checkout has something to draw before the sidecar exists.
func defaultConfig() Config {
	cfg := Config{
		Window: WindowConfig{Width: 1440, Height: 900, Center: true},
		UI:     UIConfig{Language: "zh", AddMode: "topic"},
		Layout: LayoutConfig{
			Grid:        [][]string{{"view", "panel"}},
			Sizes:       []float64{3, 1},
			PanelOffset: 0.74,
		},
		Views: map[string]ViewConfig{
			"view": {
				Kind:       "grid",
				FixedFrame: "map",
				Background: "#ffffff",
				GridCellM:  1.0,
				Zoom:       ZoomConfig{Min: 0.02, Max: 80},
				Pan:        true,
			},
		},
		Topics: map[string]TopicConfig{
			"/map":       {Type: "nav_msgs/msg/OccupancyGrid"},
			"/scan":      {Type: "sensor_msgs/msg/LaserScan"},
			"/plan":      {Type: "nav_msgs/msg/Path"},
			"/amcl_pose": {Type: "geometry_msgs/msg/PoseWithCovarianceStamped"},
		},
		Display: DisplayConfig{
			OccupancyGrid: OccupancyGridStyle{
				FreeColor: "#f2f2f2", Occupied: "#202020",
				UnknownColor: "#7a7a7a", Alpha: 0.85,
			},
			LaserScan: LaserScanStyle{Color: "#00ff88", Size: 2.5},
			Path:      PathStyle{Color: "#ff4444", Width: 2.5},
			Pose:      PoseStyle{Color: "#ffcc00", Size: 14, Covariance: true},
			Costmap: CostmapStyle{
				LowColor: "#2b6cff", HighColor: "#ff4040",
				InscribedColor: "#00e5ff", LethalColor: "#101010",
				UnknownColor: "#7a7a7a", Alpha: 0.75,
			},
			PointCloud: PointCloudStyle{
				Color: "#00d0ff", Size: 2, ColorByHeight: true,
			},
			TF: TFStyle{ShowAxes: true},
		},
		Transport: TransportConfig{},
		Tools: map[string]ToolConfig{
			"goal": {Action: "navigate_to_pose", Type: "nav2_msgs/action/NavigateToPose",
				Frame: "map"},
			"goal_topic": {Topic: "/goal_pose", Type: "geometry_msgs/msg/PoseStamped",
				Frame: "map"},
			"initial_pose": {Topic: "/initialpose",
				Type: "geometry_msgs/msg/PoseWithCovarianceStamped", Frame: "map"},
			"point": {Topic: "/clicked_point", Type: "geometry_msgs/msg/PointStamped",
				Frame: "map"},
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
	parsed, _, err := parseConfig(raw)
	if err != nil {
		return cfg, path, fmt.Errorf("%s: %w", path, err)
	}
	return parsed, path, nil
}

// utf8BOM is the three bytes a Windows editor may put in front of a JSON
// file. Written as bytes rather than a string so the source carries no BOM
// of its own.
var utf8BOM = []byte{0xEF, 0xBB, 0xBF}

// parseConfig turns one document into the typed configuration plus the generic
// tree it came from. The generic tree is what makes Save able to keep the
// `_comment` keys: see deepMerge.
func parseConfig(raw []byte) (Config, any, error) {
	// Start from zero, not from the defaults: unmarshalling into a map that is
	// already populated *adds* to it, so a file listing only /scan used to end
	// up with the default /map, /scan and /plan. A section the file names is
	// authoritative; only sections it leaves out fall back to the defaults
	// (see withDefaults).
	var cfg Config
	// Windows editors (and PowerShell's `Set-Content -Encoding UTF8`) like to
	// put a UTF-8 BOM in front. `encoding/json` treats it as an invalid
	// character, which used to make a hand-edited config silently fall back to
	// the defaults.
	raw = bytes.TrimPrefix(raw, utf8BOM)
	var doc any
	if err := json.Unmarshal(raw, &doc); err != nil {
		return defaultConfig(), nil, err
	}
	if err := json.Unmarshal(raw, &cfg); err != nil {
		return defaultConfig(), doc, err
	}
	return withDefaults(cfg, doc), doc, nil
}

// withDefaults completes a parsed document. It never *adds* to a section the
// file provided: `"topics": {}` means an empty topic list, not the defaults.
func withDefaults(cfg Config, doc any) Config {
	def := defaultConfig()

	if cfg.UI.Language == "" {
		cfg.UI.Language = def.UI.Language
	}
	if cfg.UI.AddMode == "" {
		cfg.UI.AddMode = def.UI.AddMode
	}

	if cfg.Window.Width <= 0 {
		cfg.Window.Width = def.Window.Width
	}
	if cfg.Window.Height <= 0 {
		cfg.Window.Height = def.Window.Height
	}
	// `center` defaults to true; unlike the numbers, false is a real value, so
	// look at the document to tell "absent" from "false".
	cfg.Window.Center = true
	if root, ok := doc.(map[string]any); ok {
		if win, ok := root["window"].(map[string]any); ok {
			if v, ok := win["center"].(bool); ok {
				cfg.Window.Center = v
			}
		}
	}

	if cfg.Layout.Grid == nil {
		cfg.Layout.Grid = def.Layout.Grid
	}
	if len(cfg.Layout.Sizes) == 0 {
		cfg.Layout.Sizes = def.Layout.Sizes
	}
	if cfg.Layout.PanelOffset <= 0 || cfg.Layout.PanelOffset >= 1 {
		cfg.Layout.PanelOffset = def.Layout.PanelOffset
	}

	if cfg.Views == nil {
		cfg.Views = def.Views
	} else if _, ok := cfg.Views["view"]; !ok {
		cfg.Views["view"] = def.Views["view"]
	}

	if cfg.Topics == nil {
		cfg.Topics = def.Topics
	}

	if cfg.Tools == nil {
		cfg.Tools = def.Tools
	} else {
		for name, tool := range def.Tools {
			if _, ok := cfg.Tools[name]; !ok {
				cfg.Tools[name] = tool
			}
		}
	}
	for name, tool := range cfg.Tools {
		if tool.Frame == "" {
			tool.Frame = "map"
			cfg.Tools[name] = tool
		}
	}

	// Display styles merge field by field, so a file that changes only the
	// laser colour keeps the default map colours.
	if cfg.Display.OccupancyGrid.FreeColor == "" {
		cfg.Display.OccupancyGrid.FreeColor = def.Display.OccupancyGrid.FreeColor
	}
	if cfg.Display.OccupancyGrid.Occupied == "" {
		cfg.Display.OccupancyGrid.Occupied = def.Display.OccupancyGrid.Occupied
	}
	if cfg.Display.OccupancyGrid.UnknownColor == "" {
		cfg.Display.OccupancyGrid.UnknownColor = def.Display.OccupancyGrid.UnknownColor
	}
	if cfg.Display.OccupancyGrid.Alpha == 0 {
		cfg.Display.OccupancyGrid.Alpha = def.Display.OccupancyGrid.Alpha
	}
	if cfg.Display.LaserScan.Color == "" {
		cfg.Display.LaserScan.Color = def.Display.LaserScan.Color
	}
	if cfg.Display.LaserScan.Size == 0 {
		cfg.Display.LaserScan.Size = def.Display.LaserScan.Size
	}
	if cfg.Display.Path.Color == "" {
		cfg.Display.Path.Color = def.Display.Path.Color
	}
	if cfg.Display.Path.Width == 0 {
		cfg.Display.Path.Width = def.Display.Path.Width
	}
	if cfg.Display.Pose.Color == "" {
		cfg.Display.Pose.Color = def.Display.Pose.Color
	}
	if cfg.Display.Pose.Size == 0 {
		cfg.Display.Pose.Size = def.Display.Pose.Size
	}
	if cfg.Display.Costmap.LowColor == "" {
		cfg.Display.Costmap.LowColor = def.Display.Costmap.LowColor
	}
	if cfg.Display.Costmap.HighColor == "" {
		cfg.Display.Costmap.HighColor = def.Display.Costmap.HighColor
	}
	if cfg.Display.Costmap.InscribedColor == "" {
		cfg.Display.Costmap.InscribedColor = def.Display.Costmap.InscribedColor
	}
	if cfg.Display.Costmap.LethalColor == "" {
		cfg.Display.Costmap.LethalColor = def.Display.Costmap.LethalColor
	}
	if cfg.Display.Costmap.UnknownColor == "" {
		cfg.Display.Costmap.UnknownColor = def.Display.Costmap.UnknownColor
	}
	if cfg.Display.Costmap.Alpha == 0 {
		cfg.Display.Costmap.Alpha = def.Display.Costmap.Alpha
	}
	if cfg.Display.PointCloud.Color == "" {
		cfg.Display.PointCloud.Color = def.Display.PointCloud.Color
	}
	if cfg.Display.PointCloud.Size == 0 {
		cfg.Display.PointCloud.Size = def.Display.PointCloud.Size
	}
	// `color_by_height` defaults to true; false is a real value, so read the
	// document rather than the zero value.
	cfg.Display.PointCloud.ColorByHeight = true
	if root, ok := doc.(map[string]any); ok {
		if display, ok := root["display"].(map[string]any); ok {
			if cloud, ok := display["point_cloud"].(map[string]any); ok {
				if v, ok := cloud["color_by_height"].(bool); ok {
					cfg.Display.PointCloud.ColorByHeight = v
				}
			}
		}
	}
	// `show_axes` defaults to true; false is a real value, so read the
	// document rather than the zero value.
	cfg.Display.TF.ShowAxes = true
	if root, ok := doc.(map[string]any); ok {
		if display, ok := root["display"].(map[string]any); ok {
			if tf, ok := display["tf"].(map[string]any); ok {
				if v, ok := tf["show_axes"].(bool); ok {
					cfg.Display.TF.ShowAxes = v
				}
			}
		}
	}

	if cfg.Transport.Cmd == "" {
		cfg.Transport.Cmd = def.Transport.Cmd
	}
	return cfg
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

// writeConfig saves `cfg` over `path` **without losing the file's comments**.
//
// A struct round-trip cannot do that: `_comment` keys are not fields, so
// marshalling the struct would drop every one of them the first time the user
// pressed Save. Instead the old document is read back as a generic value and
// the new configuration is deep-merged into it - keys the new value does not
// mention (the `_comment*` entries, including the ones nested inside a topic)
// survive, keys it does mention are replaced.
func writeConfig(path string, cfg Config) error {
	var previous any
	if raw, err := os.ReadFile(path); err == nil {
		// The file may carry a BOM, exactly like parseConfig allows; without
		// this trim the old document would not decode and Save would silently
		// drop every `_comment` key in it.
		_ = json.Unmarshal(bytes.TrimPrefix(raw, utf8BOM), &previous)
	} else if !os.IsNotExist(err) {
		return err
	}

	current, err := toGeneric(cfg)
	if err != nil {
		return err
	}
	merged := deepMerge(previous, current)

	body, err := json.MarshalIndent(merged, "", "  ")
	if err != nil {
		return err
	}
	body = append(body, '\n')
	return os.WriteFile(path, body, 0o644)
}

// toGeneric turns a typed value into the `any` tree encoding/json produces.
func toGeneric(v any) (any, error) {
	raw, err := json.Marshal(v)
	if err != nil {
		return nil, err
	}
	var out any
	if err := json.Unmarshal(raw, &out); err != nil {
		return nil, err
	}
	return out, nil
}

// deepMerge overlays `new` on `old`. Maps merge key by key, so a comment that
// only exists in `old` stays; everything else is replaced by `new` (arrays and
// scalars included).
func deepMerge(old, new any) any {
	oldMap, oldOK := old.(map[string]any)
	newMap, newOK := new.(map[string]any)
	if !oldOK || !newOK {
		return new
	}
	out := make(map[string]any, len(oldMap)+len(newMap))
	for k, v := range oldMap {
		out[k] = v
	}
	for k, v := range newMap {
		if prev, exists := out[k]; exists {
			out[k] = deepMerge(prev, v)
		} else {
			out[k] = v
		}
	}
	return out
}
