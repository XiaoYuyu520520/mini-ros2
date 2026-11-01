package main

import (
	"fmt"
	"math"
	"sort"
	"strconv"
	"strings"

	"fyne.io/fyne/v2"
	"fyne.io/fyne/v2/container"
	"fyne.io/fyne/v2/dialog"
	"fyne.io/fyne/v2/driver/desktop"
	"fyne.io/fyne/v2/layout"
	"fyne.io/fyne/v2/theme"
	"fyne.io/fyne/v2/widget"
)

// displayKind is one entry of the "Add a display" list. rviz calls these
// displays and the user picks a *kind* first and a topic afterwards; the
// topic is not the thing that is managed.
type displayKind struct {
	Label string
	Type  string
}

// displayKinds is the list the Add dialog offers. It is the set of message
// types the sidecar can decode, in the order a person would look for them.
var displayKinds = []displayKind{
	{Label: "Map", Type: "nav_msgs/msg/OccupancyGrid"},
	{Label: "Costmap", Type: "nav2_msgs/msg/Costmap"},
	{Label: "Costmap update", Type: "nav2_msgs/msg/CostmapUpdate"},
	{Label: "Occupancy grid update", Type: "map_msgs/msg/OccupancyGridUpdate"},
	{Label: "LaserScan", Type: "sensor_msgs/msg/LaserScan"},
	{Label: "PointCloud2", Type: "sensor_msgs/msg/PointCloud2"},
	{Label: "Path", Type: "nav_msgs/msg/Path"},
	{Label: "Pose", Type: "geometry_msgs/msg/PoseStamped"},
	{Label: "Pose with covariance", Type: "geometry_msgs/msg/PoseWithCovarianceStamped"},
	{Label: "Odometry", Type: "nav_msgs/msg/Odometry"},
	{Label: "Marker", Type: "visualization_msgs/msg/Marker"},
	{Label: "MarkerArray", Type: "visualization_msgs/msg/MarkerArray"},
}

// typeShort is the short name of a message type, the way rviz writes it in a
// display's name: "sensor_msgs/msg/LaserScan" becomes "LaserScan".
func typeShort(typeName string) string {
	if typeName == "" {
		return "(no type)"
	}
	if i := strings.LastIndex(typeName, "/"); i >= 0 && i+1 < len(typeName) {
		return typeName[i+1:]
	}
	return typeName
}

// shell is the assembled window. It is a struct rather than a function's local
// state so the layout behaviour (collapse, fit) can be exercised by a test
// without a screen.
type shell struct {
	cfg  Config
	path string
	win  fyne.Window
	view *viewport
	// split holds the panel on the left and the viewport on the right, the
	// way rviz has them: the displays are a control panel, not a sidebar of
	// the drawing.
	split     *container.Split
	accordion *widget.Accordion
	// displayList is the tree of displays. Each row is one display: a tick
	// shows it, the name says what it is.
	displayList *widget.List
	topicNames  []string
	// selectedDisplay is the display whose properties are on screen.
	selectedDisplay string
	// graphNames and graphTypes are what the sidecar says the graph holds.
	graphNames []string
	graphTypes map[string]string

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

// viewOffset is the fraction of the window the viewport takes. The panel is
// on the left, so the splitter's offset is the other fraction.
func (s *shell) viewOffset() float64 {
	return 1 - s.split.Offset
}

// setViewOffset puts the viewport at that fraction and leaves the rest to the
// panel.
func (s *shell) setViewOffset(fraction float64) {
	if fraction < 0.05 {
		fraction = 0.05
	}
	if fraction > 0.95 {
		fraction = 0.95
	}
	s.split.SetOffset(1 - fraction)
}

// collapseView squeezes the panel away: the viewport takes everything but a
// sliver, so the panel can still be dragged back.
func (s *shell) collapseView() {
	s.split.SetOffset(0.001)
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
		graphTypes: map[string]string{},
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

	// --- footer ------------------------------------------------------------
	// The footer shows the zoom and the screen scale; several places change
	// the zoom, so it is one function.
	updateFooter := func() {
		zoomLabel.SetText(fmt.Sprintf("zoom: %.1f px/m", v.Zoom()))
		dpiLabel.SetText(fmt.Sprintf("dpi: %.2fx", v.dpiScale()))
	}
	updateFooter()

	// --- displays ---------------------------------------------------------
	// The list is a tree of displays, one row each: a tick means "show
	// this", the name says what it draws. The topic it reads is a property of
	// the display, not a row of its own - that is the rviz arrangement, and
	// it is why a display can be pointed at another topic later.
	displayList := widget.NewList(
		func() int { return len(sh.topicNames) },
		func() fyne.CanvasObject {
			return container.NewHBox(
				widget.NewCheck("", nil),
				widget.NewLabel("display"),
			)
		},
		func(i widget.ListItemID, o fyne.CanvasObject) {
			if i >= len(sh.topicNames) {
				return
			}
			name := sh.topicNames[i]
			row := o.(*fyne.Container)
			check := row.Objects[0].(*widget.Check)
			label := row.Objects[1].(*widget.Label)
			topic, known := sh.cfg.Topics[name]
			// The callback is replaced on every update, because the list
			// reuses one object for many rows.
			check.OnChanged = nil
			check.SetChecked(!known || topic.enabled())
			check.OnChanged = func(on bool) { sh.setTopicEnabled(name, on) }
			label.SetText(fmt.Sprintf("%-22s %s", typeShort(topic.Type), name))
			check.Refresh()
			label.Refresh()
		},
	)
	sh.displayList = displayList

	// --- properties -------------------------------------------------------
	// The property inspector: what the selected display draws and how it
	// talks to the agent. Empty means "the type's default", which is why the
	// fields carry the default in their placeholder.
	propTitle := widget.NewLabelWithStyle("(no display selected)", fyne.TextAlignLeading, fyne.TextStyle{Bold: true})
	propEnabled := widget.NewCheck("Enabled", nil)
	propTopic := widget.NewSelect(nil, nil)
	colorEntry := widget.NewEntry()
	colorEntry.SetPlaceHolder("#00ff88 - empty = the type's colour")
	sizeEntry := widget.NewEntry()
	sizeEntry.SetPlaceHolder("point size / line width - empty = the type's")
	alphaEntry := widget.NewEntry()
	alphaEntry.SetPlaceHolder("0..1 - empty = the type's")
	historyEntry := widget.NewEntry()
	historyEntry.SetPlaceHolder("1 = only the newest message")
	reliability := widget.NewSelect([]string{"(type default)", "best_effort", "reliable"}, nil)
	durability := widget.NewSelect([]string{"(type default)", "volatile", "transient_local"}, nil)
	depthEntry := widget.NewEntry()
	depthEntry.SetPlaceHolder("history depth")
	propStatus := widget.NewLabel("")

	// topicsOfType is the topics in the graph that carry a given type: the
	// Add dialog and the topic binding both need it.
	topicsOfType := func(typeName string) []string {
		names := make([]string, 0, len(sh.graphTypes))
		for _, name := range sh.graphNames {
			if sh.graphTypes[name] == typeName {
				names = append(names, name)
			}
		}
		sort.Strings(names)
		return names
	}

	loadProps := func(name string) {
		topic, known := sh.cfg.Topics[name]
		if !known {
			return
		}
		sh.selectedDisplay = name
		propTitle.SetText(fmt.Sprintf("%s  %s", typeShort(topic.Type), name))
		propEnabled.SetChecked(topic.enabled())
		options := topicsOfType(topic.Type)
		if len(options) == 0 {
			options = []string{name}
		} else if !containsString(options, name) {
			options = append([]string{name}, options...)
		}
		propTopic.Options = options
		propTopic.SetSelected(name)
		colorEntry.SetText(topic.Color)
		sizeEntry.SetText(formatNumber(topic.PointSize))
		alphaEntry.SetText(formatNumber(topic.Alpha))
		historyEntry.SetText(formatNumber(float64(topic.History)))
		reliability.SetSelected(topic.QOS.Reliability)
		if reliability.Selected == "" {
			reliability.SetSelected("(type default)")
		}
		durability.SetSelected(topic.QOS.Durability)
		if durability.Selected == "" {
			durability.SetSelected("(type default)")
		}
		depthEntry.SetText(formatNumber(float64(topic.QOS.Depth)))
		propStatus.SetText("")
	}

	displayList.OnSelected = func(id widget.ListItemID) {
		if id < 0 || id >= len(sh.topicNames) {
			return
		}
		loadProps(sh.topicNames[id])
	}

	applyProps := func() {
		name := sh.selectedDisplay
		if name == "" {
			propStatus.SetText("pick a display first")
			return
		}
		topic, known := sh.cfg.Topics[name]
		if !known {
			return
		}
		enabled := propEnabled.Checked
		topic.Enabled = &enabled
		topic.Color = strings.TrimSpace(colorEntry.Text)
		topic.PointSize = parseNumber(sizeEntry.Text)
		topic.Width = topic.PointSize
		topic.Alpha = parseNumber(alphaEntry.Text)
		topic.History = int(parseNumber(historyEntry.Text))
		if reliability.Selected != "(type default)" {
			topic.QOS.Reliability = reliability.Selected
		} else {
			topic.QOS.Reliability = ""
		}
		if durability.Selected != "(type default)" {
			topic.QOS.Durability = durability.Selected
		} else {
			topic.QOS.Durability = ""
		}
		topic.QOS.Depth = int(parseNumber(depthEntry.Text))

		// The topic binding: a display can be pointed at another topic of
		// the same type, the way a display in rviz is repointed.
		bound := propTopic.Selected
		if bound != "" && bound != name {
			if _, exists := sh.cfg.Topics[bound]; !exists {
				sh.cfg.Topics[bound] = topic
				delete(sh.cfg.Topics, name)
				for i, entry := range sh.topicNames {
					if entry == name {
						sh.topicNames[i] = bound
					}
				}
				sort.Strings(sh.topicNames)
				name = bound
				sh.selectedDisplay = bound
				if sh.feed != nil {
					sh.feed.setEnabled(bound, enabled)
				}
			}
		}
		sh.cfg.Topics[name] = topic
		displayList.Refresh()
		// The viewport holds a copy of the per-topic styles; give it the new
		// one and let it rebuild the map textures.
		v.applyView(&sh.cfg)
		if sh.feed != nil {
			sh.feed.resubscribe()
		}
		propStatus.SetText("applied")
		sh.announce(fmt.Sprintf("properties of %s applied", name), nil)
	}

	removeDisplay := func() {
		name := sh.selectedDisplay
		if name == "" {
			return
		}
		delete(sh.cfg.Topics, name)
		for i, entry := range sh.topicNames {
			if entry == name {
				sh.topicNames = append(sh.topicNames[:i], sh.topicNames[i+1:]...)
				break
			}
		}
		sh.selectedDisplay = ""
		propTitle.SetText("(no display selected)")
		propEnabled.SetChecked(false)
		if sh.feed != nil {
			sh.feed.setEnabled(name, false)
		}
		displayList.Refresh()
		sh.announce(fmt.Sprintf("removed %s", name), nil)
	}

	properties := container.NewVBox(
		propTitle,
		propEnabled,
		widget.NewLabel("Topic:"),
		propTopic,
		widget.NewLabel("Color:"),
		colorEntry,
		widget.NewLabel("Size:"),
		sizeEntry,
		widget.NewLabel("Alpha:"),
		alphaEntry,
		widget.NewLabel("History length:"),
		historyEntry,
		widget.NewSeparator(),
		widget.NewLabel("QoS (used at the agent boundary):"),
		reliability,
		durability,
		depthEntry,
		container.NewHBox(
			widget.NewButton("Apply", applyProps),
			widget.NewButtonWithIcon("", theme.ContentRemoveIcon(), removeDisplay),
		),
		propStatus,
	)

	// --- add a display ----------------------------------------------------
	// rviz's Add button opens a list of display *types*; the topic is picked
	// afterwards. The same order here keeps the two tools familiar.
	var addDialog *dialog.ConfirmDialog
	addDisplay := func() {
		kindLabels := make([]string, 0, len(displayKinds))
		for _, kind := range displayKinds {
			kindLabels = append(kindLabels, kind.Label)
		}
		kindSelect := widget.NewSelect(kindLabels, nil)
		kindSelect.SetSelected(kindLabels[0])
		topicSelect := widget.NewSelect(nil, nil)
		hint := widget.NewLabel("")
		// how many topics the graph offered for the selected kind. The
		// dialog keeps it so a later look at the graph can say "still none"
		// instead of "none yet", which are different problems.
		loaded := false

		refreshTopics := func() {
			typeName := ""
			for _, kind := range displayKinds {
				if kind.Label == kindSelect.Selected {
					typeName = kind.Type
					break
				}
			}
			options := topicsOfType(typeName)
			topicSelect.Options = options
			topicSelect.ClearSelected()
			topicSelect.Refresh()
			if len(options) > 0 {
				topicSelect.SetSelected(options[0])
				hint.SetText(fmt.Sprintf("%d topic(s) of this type", len(options)))
			} else if loaded {
				hint.SetText(fmt.Sprintf("no %s topic in the graph (press Refresh to look again)",
					typeShort(typeName)))
			} else {
				hint.SetText("looking for topics...")
			}
		}
		kindSelect.OnChanged = func(string) { refreshTopics() }
		refreshTopics()

		content := container.NewVBox(
			widget.NewLabel("Display type:"),
			kindSelect,
			widget.NewLabel("Topic:"),
			topicSelect,
			hint,
		)
		addDialog = dialog.NewCustomConfirm("Add a display", "Add", "Cancel", content,
			func(confirmed bool) {
				if !confirmed {
					return
				}
				typeName := ""
				for _, kind := range displayKinds {
					if kind.Label == kindSelect.Selected {
						typeName = kind.Type
						break
					}
				}
				name := topicSelect.Selected
				if name == "" {
					sh.announce("pick a topic first", nil)
					return
				}
				if _, known := sh.cfg.Topics[name]; known {
					sh.announce(fmt.Sprintf("%s is already shown", name), nil)
					return
				}
				sh.cfg.Topics[name] = TopicConfig{Type: typeName}
				sh.topicNames = append(sh.topicNames, name)
				sort.Strings(sh.topicNames)
				displayList.Refresh()
				if sh.feed != nil {
					sh.feed.resubscribe()
				}
				sh.announce(fmt.Sprintf("added %s (%s)", name, typeShort(typeName)), nil)
			}, w)
		addDialog.Show()
		// The graph may have changed since the last look; ask again now that
		// the dialog is on screen, and refresh the topic list when the answer
		// arrives. This is what makes the first Add on a fresh window show the
		// topics that were already there.
		go func() {
			sh.fetchGraph(func(ok bool) {
				loaded = ok
				refreshTopics()
				// A fresh window opens on "Map", and a graph without a map
				// would show an empty topic list even though it has a scan.
				// Jump to the first kind that does have a topic.
				if ok && len(topicSelect.Options) == 0 {
					for _, kind := range displayKinds {
						if len(topicsOfType(kind.Type)) > 0 {
							kindSelect.SetSelected(kind.Label)
							break
						}
					}
				}
			})
		}()
	}

	displays := container.NewVBox(
		displayList,
		container.NewHBox(
			widget.NewButtonWithIcon("Add", theme.ContentAddIcon(), addDisplay),
			widget.NewButtonWithIcon("", theme.ViewRefreshIcon(), func() {
				go func() { sh.fetchGraph(nil) }()
			}),
		),
	)

	// --- view -------------------------------------------------------------
	// The view is a property of the drawing, not of a display, so it has its
	// own section: the fixed frame, the grid and the background.
	frameEntry := widget.NewEntry()
	frameEntry.SetText(fixedFrameOf(cfg))
	gridEntry := widget.NewEntry()
	gridEntry.SetText(formatNumber(float64(gridCellOf(cfg))))
	backgroundEntry := widget.NewEntry()
	backgroundEntry.SetText(backgroundOf(cfg))
	viewStatus := widget.NewLabel("")
	applyView := func() {
		fresh := sh.cfg
		view, ok := fresh.Views["view"]
		if !ok {
			view = ViewConfig{}
		}
		if frame := strings.TrimSpace(frameEntry.Text); frame != "" {
			view.FixedFrame = frame
		}
		if cell := parseNumber(gridEntry.Text); cell > 0 {
			view.GridCellM = cell
		}
		if background := strings.TrimSpace(backgroundEntry.Text); background != "" {
			view.Background = background
		}
		fresh.Views["view"] = view
		sh.cfg = fresh
		v.applyView(&fresh)
		if sh.feed != nil {
			sh.feed.applyFixedFrame(fresh)
		}
		viewStatus.SetText("applied")
		sh.announce("view applied", nil)
	}
	viewPanel := container.NewVBox(
		widget.NewLabel("Fixed frame:"),
		frameEntry,
		widget.NewLabel("Grid cell (m):"),
		gridEntry,
		widget.NewLabel("Background:"),
		backgroundEntry,
		container.NewHBox(
			widget.NewButton("Apply", applyView),
			widget.NewButton("Fit view", func() {
				v.Fit()
				updateFooter()
			}),
		),
		viewStatus,
	)

	connection := container.NewVBox(
		widget.NewLabel("sidecar: not started"),
		widget.NewLabel("agent:   -"),
		widget.NewLabel("graph:   - topics"),
	)

	// --- panel ------------------------------------------------------------
	// rviz's order: the displays are at the top, the properties of the
	// selected display right below them, then the other sections.
	accordion := widget.NewAccordion(
		widget.NewAccordionItem("Displays", displays),
		widget.NewAccordionItem("Properties", properties),
		widget.NewAccordionItem("View", viewPanel),
		widget.NewAccordionItem("Connection", connection),
	)
	accordion.Open(0)
	accordion.Open(1)
	sh.accordion = accordion

	// The panel is on the left and the viewport fills the rest, the way
	// rviz has it.
	split := container.NewHSplit(accordion, v)
	sh.split = split
	sh.setViewOffset(sh.savedOffset)

	sh.togglePanel = func() {
		if sh.collapsed {
			sh.setViewOffset(sh.savedOffset)
			sh.collapsed = false
		} else {
			sh.savedOffset = sh.viewOffset()
			sh.collapseView()
			sh.collapsed = true
		}
		split.Refresh()
	}
	togglePanel := sh.togglePanel

	// --- config: load / save ----------------------------------------------
	// `apply` re-reads everything that is derived from the config: the
	// viewport's view settings, the display list, the window size, the splitter
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
		displayList.Refresh()
		frameEntry.SetText(fixedFrameOf(&fresh))
		gridEntry.SetText(formatNumber(float64(gridCellOf(&fresh))))
		backgroundEntry.SetText(backgroundOf(&fresh))

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
			sh.setViewOffset(off)
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
	//
	// The tools are buttons with their names on them, not icons: an
	// icon-only toolbar is a puzzle, and the names are the ones rviz uses
	// ("Move Camera", "Select", "Publish Point", "2D Goal Pose", "2D Pose
	// Estimate"). The rest of the bar keeps to icons, because those actions
	// are not what a person looks for by name.
	cameraButton := widget.NewButtonWithIcon("Move Camera", theme.MoveUpIcon(), func() {
		v.setTool(toolPan)
	})
	selectButton := widget.NewButtonWithIcon("Select", theme.SearchIcon(), func() {
		v.setTool(toolSelect)
	})
	pointButton := widget.NewButtonWithIcon("Publish Point", theme.MailSendIcon(), func() {
		v.setTool(toolPoint)
	})
	goalButton := widget.NewButtonWithIcon("2D Goal Pose", theme.NavigateNextIcon(), func() {
		v.setTool(toolGoal)
	})
	poseButton := widget.NewButtonWithIcon("2D Pose Estimate", theme.HomeIcon(), func() {
		v.setTool(toolPose)
	})
	toolbar := container.NewHBox(
		cameraButton,
		selectButton,
		pointButton,
		widget.NewSeparator(),
		goalButton,
		poseButton,
		widget.NewSeparator(),
		widget.NewButtonWithIcon("", theme.FolderOpenIcon(), func() {
			err := sh.reload()
			sh.notify("loaded", err)
			if err == nil {
				v.Fit()
				updateFooter()
			}
		}),
		widget.NewButtonWithIcon("", theme.DocumentSaveIcon(), func() {
			sh.notify("saved", sh.save())
		}),
		widget.NewSeparator(),
		widget.NewButtonWithIcon("", theme.ViewRefreshIcon(), func() {
			v.Fit()
			updateFooter()
		}),
		widget.NewButtonWithIcon("", theme.ContentAddIcon(), addDisplay),
		layout.NewSpacer(),
		widget.NewButtonWithIcon("", theme.MenuIcon(), togglePanel),
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

	// The tool label follows the viewport's tool state, and the buttons show
	// which one is armed: a tool that is armed is the one the next click in
	// the viewport belongs to.
	toolButtons := map[toolMode]*widget.Button{
		toolPan:    cameraButton,
		toolSelect: selectButton,
		toolPoint:  pointButton,
		toolGoal:   goalButton,
		toolPose:   poseButton,
	}
	v.onToolChanged = func(toolMode) {
		for mode, button := range toolButtons {
			if mode == v.Tool() {
				button.Importance = widget.HighImportance
			} else {
				button.Importance = widget.MediumImportance
			}
			button.Refresh()
		}
		toolLabel.SetText("tool: " + toolName(v.Tool()))
		toolLabel.Refresh()
	}
	v.onPlace = func(mode toolMode, x, y, yaw float64) {
		sh.publishTool(mode, x, y, yaw)
	}
	// The Select tool picks the display under the pointer; the shell knows the
	// scene, the viewport only knows the geometry.
	v.onSelect = func(x, y float64) {
		name, ok := sh.pickDisplay(x, y)
		if !ok {
			sh.announce(fmt.Sprintf("nothing at %.2f, %.2f", x, y), nil)
			return
		}
		for i, entry := range sh.topicNames {
			if entry == name {
				displayList.Select(i)
				break
			}
		}
		v.setPicked(x, y)
		sh.announce(fmt.Sprintf("selected %s at %.2f, %.2f", name, x, y), nil)
	}
	v.onPublishPoint = func(x, y float64) {
		sh.publishPoint(x, y)
	}

	sh.feed = newFeedController(v, scene, func() Config { return sh.cfg }, sidecarLabel, frameLabel)
	sh.feed.onConnected = func() {
		go func() { sh.fetchGraph(nil) }()
	}
	sh.feed.start()
	w.SetCloseIntercept(func() {
		sh.feed.close()
		w.Close()
	})
	return sh
}

// containsString reports whether the list has the name.
func containsString(names []string, name string) bool {
	for _, entry := range names {
		if entry == name {
			return true
		}
	}
	return false
}

// gridCellOf is the grid spacing of the view.
func gridCellOf(cfg *Config) float64 {
	if view, ok := cfg.Views["view"]; ok && view.GridCellM > 0 {
		return view.GridCellM
	}
	return 1
}

// backgroundOf is the background colour of the view.
func backgroundOf(cfg *Config) string {
	if view, ok := cfg.Views["view"]; ok && view.Background != "" {
		return view.Background
	}
	return "#1e1e1e"
}

// fetchGraph asks the sidecar what the graph holds and remembers it. The
// topic list and the Add dialog both read that list.
func (s *shell) fetchGraph(after func(ok bool)) {
	if s.feed == nil {
		return
	}
	frames, err := s.feed.graphTopics()
	if err != nil {
		fyne.Do(func() {
			if after != nil {
				after(false)
			} else {
				s.announce("topic list failed", err)
			}
		})
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
		s.graphNames = names
		s.graphTypes = types
		if after != nil {
			after(true)
		}
	})
}

// pickDisplay finds the display that owns the world point: the one whose
// geometry is closest to it. A grid counts as owning the whole cell it is
// in, so a click on the map picks the map.
func (s *shell) pickDisplay(x, y float64) (string, bool) {
	if s.feed == nil {
		return "", false
	}
	snapshot := s.feed.scene.Snapshot()
	best := ""
	bestDistance := 0.0

	consider := func(name string, px, py float64) {
		distance := (px-x)*(px-x) + (py-y)*(py-y)
		if best == "" || distance < bestDistance {
			best = name
			bestDistance = distance
		}
	}

	for name, layer := range snapshot.Scans {
		if layer == nil {
			continue
		}
		for i := 0; i+1 < len(layer.Points); i += 2 {
			consider(name, layer.Points[i], layer.Points[i+1])
		}
	}
	for name, layer := range snapshot.Clouds {
		if layer == nil {
			continue
		}
		for i := 0; i+1 < len(layer.Points); i += 2 {
			consider(name, layer.Points[i], layer.Points[i+1])
		}
	}
	for name, layer := range snapshot.Poses {
		if layer != nil {
			consider(name, layer.X, layer.Y)
		}
	}
	for name, layer := range snapshot.Odometry {
		if layer != nil {
			consider(name, layer.X, layer.Y)
		}
	}
	for name, layer := range snapshot.Paths {
		if layer == nil {
			continue
		}
		for i := 0; i+1 < len(layer.Poses); i += 3 {
			consider(name, layer.Poses[i], layer.Poses[i+1])
		}
	}
	for topic, byID := range snapshot.Markers {
		for _, layer := range byID {
			if layer != nil {
				consider(topic, layer.X, layer.Y)
			}
		}
	}

	// A point inside a grid belongs to that grid, and the grid wins over a
	// scan point that happens to be near the click.
	for name, layer := range snapshot.Maps {
		if layer == nil || layer.Resolution <= 0 {
			continue
		}
		col := int(math.Floor((x - layer.OriginX) / layer.Resolution))
		row := int(math.Floor((y - layer.OriginY) / layer.Resolution))
		if col >= 0 && row >= 0 && col < layer.Width && row < layer.Height {
			return name, true
		}
	}
	for name, layer := range snapshot.Costmaps {
		if layer == nil || layer.Resolution <= 0 {
			continue
		}
		col := int(math.Floor((x - layer.OriginX) / layer.Resolution))
		row := int(math.Floor((y - layer.OriginY) / layer.Resolution))
		if col >= 0 && row >= 0 && col < layer.Width && row < layer.Height {
			return name, true
		}
	}
	if best == "" {
		return "", false
	}
	// Only a point that is really close counts: a click in empty space must
	// not pick whatever happened to be nearest.
	if bestDistance > 0.25 {
		return "", false
	}
	return best, true
}

// publishPoint sends the clicked point, the way rviz's Publish Point tool
// does.
func (s *shell) publishPoint(x, y float64) {
	if s.feed == nil {
		return
	}
	tool, ok := s.cfg.Tools["point"]
	if !ok {
		tool = ToolConfig{Topic: "/clicked_point", Type: "geometry_msgs/msg/PointStamped"}
	}
	topic := tool.Topic
	if topic == "" {
		topic = "/clicked_point"
	}
	typeName := tool.Type
	if typeName == "" {
		typeName = "geometry_msgs/msg/PointStamped"
	}
	frame := tool.Frame
	if frame == "" {
		frame = fixedFrameOf(&s.cfg)
	}
	if err := s.feed.publishPoint(topic, typeName, frame, x, y, 0); err != nil {
		s.announce("publish point failed", err)
		return
	}
	s.announce(fmt.Sprintf("point published on %s: %.2f, %.2f", topic, x, y), nil)
}

// snapshot is the configuration as it stands on screen right now: the window's
// current size, the splitter's current position, and the display list as the
// panel shows it. Save writes this, not the file that was loaded, so a resize
// before saving is not lost.
//
// Per-display settings come straight from the loaded configuration, since
// the property inspector writes them back into it.
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
		cfg.Layout.PanelOffset = s.viewOffset()
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

// setTopicEnabled is the checkbox in the display list. It writes the choice
// into the configuration - so it survives a save - and tells the sidecar to
// stop or start delivering the topic.
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
