package main

import (
	"fmt"
	"sort"
	"strconv"
	"strings"

	"fyne.io/fyne/v2"
	"fyne.io/fyne/v2/container"
	"fyne.io/fyne/v2/driver/desktop"
	"fyne.io/fyne/v2/theme"
	"fyne.io/fyne/v2/widget"
)

// shell is the assembled window. It is a struct rather than a function's local
// state so the layout behaviour (collapse, fit) can be exercised by a test
// without a screen.
type shell struct {
	cfg          Config
	path         string
	win          fyne.Window
	view         *viewport
	split        *container.Split
	accordion    *widget.Accordion
	topicList    *widget.List
	topicNames   []string
	savedOffset  float64
	collapsed    bool
	togglePanel  func()
	updateFooter func()
	reload       func() error
	save         func() error
	notify       func(string, error)
	announce     func(string, error)
	feed         *feedController
}

// sortedTopicNames is the list order the panel shows: stable, so a reload does
// not shuffle the entries under the pointer.
func sortedTopicNames(cfg *Config) []string {
	names := make([]string, 0, len(cfg.Topics))
	for name := range cfg.Topics {
		names = append(names, name)
	}
	sort.Strings(names)
	return names
}

// buildUI assembles the shell: a toolbar, a 2D viewport, a collapsible side
// panel of accordion sections, and a status bar. Nothing here talks to the
// sidecar yet - the point is the frame the displays will be dropped into.
func buildUI(a fyne.App, w fyne.Window, cfg *Config, cfgPath string) *shell {
	v := newViewport(cfg)
	scene := NewScene()
	v.setScene(scene)
	sh := &shell{
		cfg: *cfg, path: cfgPath, win: w, view: v,
		topicNames: sortedTopicNames(cfg), savedOffset: cfg.Layout.PanelOffset,
	}
	if sh.savedOffset <= 0 || sh.savedOffset >= 1 {
		sh.savedOffset = 0.74
	}

	// --- status bar -------------------------------------------------------
	zoomLabel := widget.NewLabel("zoom: -")
	cursorLabel := widget.NewLabel("x: -  y: -")
	dpiLabel := widget.NewLabel("dpi: -")
	v.onCursor = func(x, y float64) {
		cursorLabel.SetText(fmt.Sprintf("x: %+8.2f  y: %+8.2f", x, y))
	}

	statusMsg := widget.NewLabelWithStyle("ready", fyne.TextAlignLeading, fyne.TextStyle{Monospace: true})
	sidecarLabel := widget.NewLabel("sidecar: starting")
	frameLabel := widget.NewLabel("frame: " + fixedFrameOf(cfg))
	toolLabel := widget.NewLabel("tool: none")
	status := container.NewHBox(
		statusMsg,
		widget.NewSeparator(),
		sidecarLabel,
		widget.NewSeparator(),
		frameLabel,
		widget.NewSeparator(),
		toolLabel,
		widget.NewSeparator(),
		cursorLabel,
		widget.NewSeparator(),
		zoomLabel,
		widget.NewSeparator(),
		dpiLabel,
	)

	// --- side panel -------------------------------------------------------
	// The topic list is the "add a display" surface: names come from config,
	// and the button appends one so the list mechanics can be felt before the
	// sidecar supplies real topics. It reads through the shell so a reload can
	// replace the slice without the list holding a stale copy.
	// The topic list is a list of checkboxes: a tick means "show this", and
	// unticking stops the sidecar delivering it and drops the last frame.
	topicList := widget.NewList(
		func() int { return len(sh.topicNames) },
		func() fyne.CanvasObject { return widget.NewCheck("", nil) },
		func(i widget.ListItemID, o fyne.CanvasObject) {
			if i >= len(sh.topicNames) {
				return
			}
			name := sh.topicNames[i]
			check := o.(*widget.Check)
			check.Text = name
			// The callback is replaced on every update, because the list
			// reuses one object for many rows.
			check.OnChanged = nil
			topic, known := sh.cfg.Topics[name]
			check.SetChecked(!known || topic.enabled())
			check.OnChanged = func(on bool) { sh.setTopicEnabled(name, on) }
			check.Refresh()
		},
	)
	sh.topicList = topicList

	// The topic picker. It lists what the graph has and adds the chosen one
	// to the configuration, so a new topic needs no restart and no hand
	// editing.
	topicTypes := map[string]string{}
	topicTypeLabel := widget.NewLabel("(select a topic)")
	topicSelect := widget.NewSelect(nil, func(name string) {
		if typeName := topicTypes[name]; typeName != "" {
			topicTypeLabel.SetText(typeName)
		} else {
			topicTypeLabel.SetText("(select a topic)")
		}
	})
	fillTopics := func() {
		go func() {
			frames, err := sh.feed.graphTopics()
			if err != nil {
				sh.announce("topic list failed", err)
				return
			}
			names := make([]string, 0, len(frames))
			types := make(map[string]string, len(frames))
			for _, entry := range frames {
				if entry.Name == "/tf" || entry.Name == "/tf_static" {
					continue
				}
				if entry.Type == "" {
					continue
				}
				names = append(names, entry.Name)
				types[entry.Name] = entry.Type
			}
			sort.Strings(names)
			fyne.Do(func() {
				topicTypes = types
				topicSelect.Options = names
				topicSelect.ClearSelected()
				topicSelect.Refresh()
				topicTypeLabel.SetText(fmt.Sprintf("%d topic(s) in the graph", len(names)))
			})
		}()
	}
	var refreshPropTopics func()
	addTopic := func() {
		name := topicSelect.Selected
		if name == "" {
			sh.announce("pick a topic first", nil)
			return
		}
		typeName := topicTypes[name]
		if typeName == "" {
			sh.announce(fmt.Sprintf("%s has no type yet", name), nil)
			return
		}
		if _, known := sh.cfg.Topics[name]; !known {
			sh.cfg.Topics[name] = TopicConfig{Type: typeName}
			sh.topicNames = append(sh.topicNames, name)
			sort.Strings(sh.topicNames)
		}
		topicList.Refresh()
		if refreshPropTopics != nil {
			refreshPropTopics()
		}
		sh.feed.resubscribe()
		sh.announce(fmt.Sprintf("added %s (%s)", name, typeName), nil)
	}
	// The display properties. Empty means "the type's style", so a topic only
	// carries what it wants to change.
	propTopic := widget.NewSelect(nil, nil)
	colorEntry := widget.NewEntry()
	colorEntry.SetPlaceHolder("#00ff88 - empty = the type's colour")
	sizeEntry := widget.NewEntry()
	sizeEntry.SetPlaceHolder("point size / line width - empty = the type's")
	alphaEntry := widget.NewEntry()
	alphaEntry.SetPlaceHolder("0..1 - empty = the type's")
	propStatus := widget.NewLabel("")

	refreshPropTopics = func() {
		propTopic.Options = append([]string(nil), sh.topicNames...)
		propTopic.ClearSelected()
		propTopic.Refresh()
	}
	loadProps := func(name string) {
		topic, known := sh.cfg.Topics[name]
		if !known {
			return
		}
		colorEntry.SetText(topic.Color)
		sizeEntry.SetText(formatNumber(topic.PointSize))
		alphaEntry.SetText(formatNumber(topic.Alpha))
	}
	propTopic.OnChanged = loadProps
	applyProps := func() {
		name := propTopic.Selected
		if name == "" {
			propStatus.SetText("pick a topic first")
			return
		}
		topic, known := sh.cfg.Topics[name]
		if !known {
			return
		}
		topic.Color = strings.TrimSpace(colorEntry.Text)
		topic.PointSize = parseNumber(sizeEntry.Text)
		topic.Width = topic.PointSize
		topic.Alpha = parseNumber(alphaEntry.Text)
		sh.cfg.Topics[name] = topic
		// The viewport holds a copy of the per-topic styles; give it the new
		// one and let it rebuild the map textures.
		v.applyView(&sh.cfg)
		propStatus.SetText("applied")
		sh.announce(fmt.Sprintf("display properties of %s applied", name), nil)
	}
	refreshPropTopics()

	displays := container.NewVBox(
		widget.NewLabel("Add a topic from the graph:"),
		topicSelect,
		topicTypeLabel,
		container.NewHBox(
			widget.NewButton("Refresh", func() { fillTopics() }),
			widget.NewButton("Add", addTopic),
		),
		widget.NewSeparator(),
		widget.NewLabel("Display properties:"),
		propTopic,
		colorEntry,
		sizeEntry,
		alphaEntry,
		container.NewHBox(
			widget.NewButton("Reload", func() {
				refreshPropTopics()
				if len(sh.topicNames) > 0 {
					loadProps(sh.topicNames[0])
				}
			}),
			widget.NewButton("Apply", applyProps),
		),
		propStatus,
	)
	connection := container.NewVBox(
		widget.NewLabel("sidecar: not started"),
		widget.NewLabel("agent:   -"),
		widget.NewLabel("graph:   - topics"),
	)

	accordion := widget.NewAccordion(
		widget.NewAccordionItem("Topics", topicList),
		widget.NewAccordionItem("Displays", displays),
		widget.NewAccordionItem("Connection", connection),
	)
	accordion.Open(0)
	sh.accordion = accordion

	// --- split + collapse -------------------------------------------------
	split := container.NewHSplit(v, accordion)
	split.SetOffset(sh.savedOffset)
	sh.split = split

	updateFooter := func() {
		zoomLabel.SetText(fmt.Sprintf("zoom: %.1f px/m", v.Zoom()))
		dpiLabel.SetText(fmt.Sprintf("dpi: %.2fx", v.dpiScale()))
	}
	updateFooter()

	sh.togglePanel = func() {
		if sh.collapsed {
			split.SetOffset(sh.savedOffset)
			sh.collapsed = false
		} else {
			sh.savedOffset = split.Offset
			split.SetOffset(0.999)
			sh.collapsed = true
		}
		split.Refresh()
	}
	togglePanel := sh.togglePanel

	// --- config: load / save ----------------------------------------------
	// `apply` re-reads everything that is derived from the config: the
	// viewport's view settings, the topic list, the window size, the splitter
	// position, and the footer (the zoom limits may have changed).
	sh.notify = func(what string, err error) {
		if err != nil {
			statusMsg.SetText(fmt.Sprintf("%s failed: %v", what, err))
			statusMsg.Importance = widget.DangerImportance
		} else {
			statusMsg.SetText(fmt.Sprintf("%s %s", what, sh.path))
			statusMsg.Importance = widget.LowImportance
		}
		statusMsg.Refresh()
	}

	// announce is the same status line without the path suffix: it is for
	// things that are not about the configuration file.
	sh.announce = func(what string, err error) {
		if err != nil {
			statusMsg.SetText(fmt.Sprintf("%s: %v", what, err))
			statusMsg.Importance = widget.DangerImportance
		} else {
			statusMsg.SetText(what)
			statusMsg.Importance = widget.LowImportance
		}
		statusMsg.Refresh()
	}

	apply := func(fresh Config) {
		sh.cfg = fresh
		v.applyView(&fresh)
		sh.topicNames = sortedTopicNames(&fresh)
		topicList.Refresh()

		// A loaded file describes a workspace, so its window size and splitter
		// position are applied too. A reload that did not would look like it
		// had done nothing.
		if fresh.Window.Width > 0 && fresh.Window.Height > 0 {
			w.Resize(fyne.NewSize(float32(fresh.Window.Width), float32(fresh.Window.Height)))
		}
		if off := fresh.Layout.PanelOffset; off > 0 && off < 1 {
			// The loaded file's layout wins, including expanding a panel the
			// user had collapsed: otherwise the new offset is invisible.
			sh.savedOffset = off
			sh.collapsed = false
			split.SetOffset(off)
		}
		updateFooter()
	}

	sh.reload = func() error {
		fresh, path, err := loadConfig(sh.path)
		if err != nil {
			return err
		}
		sh.path = path
		apply(fresh)
		w.SetTitle(fmt.Sprintf("mini-ros2 viz — %s", path))
		return nil
	}

	sh.save = func() error {
		// The path in the title is the truth: Save writes where Load read.
		return writeConfig(sh.path, sh.snapshot())
	}

	// --- toolbar ----------------------------------------------------------
	toolbar := widget.NewToolbar(
		// The tools come first: they are what the pointer is for, and their
		// position is stable enough for the screenshot tests to find them.
		widget.NewToolbarAction(theme.NavigateNextIcon(), func() {
			v.setTool(toolGoal)
		}),
		widget.NewToolbarAction(theme.HomeIcon(), func() {
			v.setTool(toolPose)
		}),
		widget.NewToolbarSeparator(),
		widget.NewToolbarAction(theme.FolderOpenIcon(), func() {
			err := sh.reload()
			sh.notify("loaded", err)
			if err == nil {
				v.Fit()
				updateFooter()
			}
		}),
		widget.NewToolbarAction(theme.DocumentSaveIcon(), func() {
			sh.notify("saved", sh.save())
		}),
		widget.NewToolbarSeparator(),
		widget.NewToolbarAction(theme.ViewRefreshIcon(), func() {
			v.Fit()
			updateFooter()
		}),
		widget.NewToolbarAction(theme.ContentAddIcon(), func() {
			name := fmt.Sprintf("/new/topic/%d", len(sh.topicNames)+1)
			sh.topicNames = append(sh.topicNames, name)
			topicList.Refresh()
		}),
		widget.NewToolbarSpacer(),
		widget.NewToolbarAction(theme.MenuIcon(), togglePanel),
	)

	// --- layout -----------------------------------------------------------
	root := container.NewBorder(toolbar, status, nil, nil, split)
	w.SetContent(root)

	// F8 re-centres, F9 collapses the side panel. They exist because a
	// visualiser is used with the pointer already in the viewport, and because
	// a keyboard path is what makes these behaviours testable without driving
	// the mouse.
	w.Canvas().SetOnTypedKey(func(e *fyne.KeyEvent) {
		switch e.Name {
		case fyne.KeyEscape:
			v.cancelTool()
		case fyne.KeyF8:
			v.Fit()
			updateFooter()
		case fyne.KeyF9:
			togglePanel()
		case fyne.KeyF5:
			err := sh.reload()
			sh.notify("loaded", err)
			if err == nil {
				v.Fit()
			}
		}
	})

	// Ctrl+S / Ctrl+O. A visualiser is used with the pointer in the viewport,
	// so both config paths need a shortcut as well as a toolbar button.
	w.Canvas().AddShortcut(
		&desktop.CustomShortcut{KeyName: fyne.KeyS, Modifier: fyne.KeyModifierControl},
		func(fyne.Shortcut) { sh.notify("saved", sh.save()) },
	)
	w.Canvas().AddShortcut(
		&desktop.CustomShortcut{KeyName: fyne.KeyO, Modifier: fyne.KeyModifierControl},
		func(fyne.Shortcut) {
			err := sh.reload()
			sh.notify("loaded", err)
			if err == nil {
				v.Fit()
			}
		},
	)

	w.SetTitle(fmt.Sprintf("mini-ros2 viz — %s", cfgPath))
	sh.updateFooter = updateFooter

	// The tool label follows the viewport's tool state.
	v.onToolChanged = func(toolMode) {
		switch v.Tool() {
		case toolGoal:
			toolLabel.SetText("tool: 2D Goal")
		case toolPose:
			toolLabel.SetText("tool: 2D Pose Estimate")
		default:
			toolLabel.SetText("tool: none")
		}
		toolLabel.Refresh()
	}
	v.onPlace = func(mode toolMode, x, y, yaw float64) {
		sh.publishTool(mode, x, y, yaw)
	}

	sh.feed = newFeedController(v, scene, func() Config { return sh.cfg }, sidecarLabel, frameLabel)
	sh.feed.onConnected = fillTopics
	sh.feed.start()
	w.SetCloseIntercept(func() {
		sh.feed.close()
		w.Close()
	})
	return sh
}

// snapshot is the configuration as it stands on screen right now: the window's
// current size, the splitter's current position, and the topic list as the
// panel shows it. Save writes this, not the file that was loaded, so a resize
// before saving is not lost.
//
// Per-topic settings (type, qos, colour) are not editable in the shell, so they
// come straight from the loaded configuration; a name that is not in it can
// only have come from the + button and is saved with an empty type.
func (s *shell) snapshot() Config {
	cfg := s.cfg
	if s.win != nil {
		size := s.win.Canvas().Size()
		if size.Width >= 1 && size.Height >= 1 {
			cfg.Window.Width = int(size.Width)
			cfg.Window.Height = int(size.Height)
		}
	}
	// A collapsed panel is not a layout: remember where it was open, or the
	// next start would open with the panel squeezed to the edge.
	if s.collapsed {
		cfg.Layout.PanelOffset = s.savedOffset
	} else {
		cfg.Layout.PanelOffset = s.split.Offset
	}

	topics := make(map[string]TopicConfig, len(s.topicNames))
	for _, name := range s.topicNames {
		if t, ok := s.cfg.Topics[name]; ok {
			topics[name] = t
		} else {
			topics[name] = TopicConfig{Color: "#ffffff"}
		}
	}
	cfg.Topics = topics
	return cfg
}

// setTopicEnabled is the checkbox in the topic list. It writes the choice into
// the configuration - so it survives a save - and tells the sidecar to stop or
// start delivering the topic.
func (s *shell) setTopicEnabled(name string, on bool) {
	topic, known := s.cfg.Topics[name]
	if !known {
		return
	}
	enabled := on
	topic.Enabled = &enabled
	s.cfg.Topics[name] = topic
	if s.feed != nil {
		s.feed.setEnabled(name, on)
	}
}

// publishTool sends a goal or an initial pose. The topic and the type come
// from the configuration (`tools.goal_topic` and `tools.initial_pose`), and
// the frame is the tool's own - a goal belongs in the map frame even when the
// view is showing another one.
func (s *shell) publishTool(mode toolMode, x, y, yaw float64) {
	key := "goal_topic"
	if mode == toolPose {
		key = "initial_pose"
	}
	tool, ok := s.cfg.Tools[key]
	if !ok || tool.Topic == "" {
		s.announce(fmt.Sprintf("%s: not configured", key), nil)
		return
	}
	frame := tool.Frame
	if frame == "" {
		frame = "map"
	}
	typeName := tool.Type
	if typeName == "" {
		typeName = "geometry_msgs/msg/PoseStamped"
		if mode == toolPose {
			typeName = "geometry_msgs/msg/PoseWithCovarianceStamped"
		}
	}
	err := s.feed.publishTool(tool.Topic, typeName, frame, x, y, yaw)
	if err != nil {
		s.announce(fmt.Sprintf("%s failed", key), err)
		return
	}
	s.announce(fmt.Sprintf("%s sent: x %.2f  y %.2f  yaw %.2f", key, x, y, yaw), nil)
}

// parseNumber reads a field the user typed. An empty field means "no
// override", which is zero.
func parseNumber(text string) float64 {
	text = strings.TrimSpace(text)
	if text == "" {
		return 0
	}
	value, err := strconv.ParseFloat(text, 64)
	if err != nil {
		return 0
	}
	return value
}

// formatNumber is the inverse: zero means "nothing to show", so the field is
// left empty.
func formatNumber(value float64) string {
	if value == 0 {
		return ""
	}
	return strconv.FormatFloat(value, 'g', -1, 64)
}
