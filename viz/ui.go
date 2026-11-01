package main

import (
	"fmt"
	"image/color"
	"math"
	"os"
	"sort"
	"strconv"
	"strings"
	"time"

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

// displayKindFor is the display a topic type maps to, if there is one. It is
// what makes "By topic" possible: the type comes from the graph.
func displayKindFor(typeName string) (displayKind, bool) {
	for _, kind := range displayKinds {
		if kind.Type == typeName {
			return kind, true
		}
	}
	return displayKind{}, false
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
	graphLabel *widget.Label

	// addMode is which half of the Add dialog opens first; it is remembered.
	addMode string
	// autoSave writes the configuration without a status message: the memory
	// features (displays, dialog mode, view) must not depend on Ctrl+S.
	autoSave func()

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
	switch cfg.UI.AddMode {
	case "type":
		sh.addMode = "type"
	default:
		sh.addMode = "topic"
	}
	// A restart opens where the last session was looking.
	if view, ok := sh.cfg.Views["view"]; ok {
		v.setView(view.CenterX, view.CenterY, view.Scale)
	}
	if sh.savedOffset <= 0 || sh.savedOffset >= 1 {
		sh.savedOffset = 0.74
	}

	// --- status bar -------------------------------------------------------
	zoomLabel := widget.NewLabel(tr("zoom: -", "缩放: -"))
	cursorLabel := widget.NewLabel("x: -  y: -")
	dpiLabel := widget.NewLabel(tr("dpi: -", "缩放比: -"))
	v.onCursor = func(x, y float64) {
		cursorLabel.SetText(fmt.Sprintf("x: %+8.2f  y: %+8.2f", x, y))
	}

	statusMsg := widget.NewLabelWithStyle(tr("ready", "就绪"), fyne.TextAlignLeading, fyne.TextStyle{Monospace: true})
	sidecarLabel := widget.NewLabel(tr("sidecar: starting", "数据边车: 启动中"))
	frameLabel := widget.NewLabel(tr("frame: ", "坐标系: ") + fixedFrameOf(cfg))
	toolLabel := widget.NewLabel(tr("tool: none", "工具: 无"))
	// The diagnostics button sits at the right edge of the status bar: when
	// something is not on screen, what a bug report needs is the graph the
	// sidecar sees and the displays this window holds, as text.
	diagnosticsButton := widget.NewButtonWithIcon(tr("Copy diagnostics", "复制诊断信息"),
		theme.ContentCopyIcon(), func() { sh.copyDiagnostics() })
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
		layout.NewSpacer(),
		diagnosticsButton,
	)

	// --- footer ------------------------------------------------------------
	// The footer shows the zoom and the screen scale; several places change
	// the zoom, so it is one function.
	updateFooter := func() {
		zoomLabel.SetText(fmt.Sprintf(tr("zoom: %.1f px/m", "缩放: %.1f 像素/米"), v.Zoom()))
		dpiLabel.SetText(fmt.Sprintf(tr("dpi: %.2fx", "缩放比: %.2fx"), v.dpiScale()))
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
	propEnabled := widget.NewCheck(tr("Enabled", "显示"), nil)
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
			propStatus.SetText(tr("pick a display first", "请先选择一个显示"))
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
		propStatus.SetText(tr("applied", "已应用"))
		sh.announce(fmt.Sprintf("properties of %s applied", name), nil)
		if sh.autoSave != nil {
			sh.autoSave()
		}
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
		sh.announce(fmt.Sprintf(tr("removed %s", "已移除 %s"), name), nil)
		if sh.autoSave != nil {
			sh.autoSave()
		}
	}

	properties := container.NewVBox(
		propTitle,
		propEnabled,
		widget.NewLabel(tr("Topic:", "话题:")),
		propTopic,
		widget.NewLabel(tr("Color:", "颜色:")),
		container.NewBorder(nil, nil, nil,
			widget.NewButton(tr("Pick...", "选颜色..."), func() {
				sh.pickColor(colorEntry.Text, func(hex string) {
					colorEntry.SetText(hex)
					applyProps()
				})
			}), colorEntry),
		widget.NewLabel(tr("Size:", "大小:")),
		sizeEntry,
		widget.NewLabel(tr("Alpha:", "透明度:")),
		alphaEntry,
		widget.NewLabel(tr("History length:", "历史帧数:")),
		historyEntry,
		widget.NewSeparator(),
		widget.NewLabel(tr("QoS (used at the agent boundary):", "QoS（在 agent 边界生效）:")),
		reliability,
		durability,
		depthEntry,
		container.NewHBox(
			widget.NewButton(tr("Apply", "应用"), applyProps),
			widget.NewButtonWithIcon("", theme.ContentRemoveIcon(), removeDisplay),
		),
		propStatus,
	)

	// --- add a display ----------------------------------------------------
	// rviz's Add dialog has two ways in: pick a display type and then its
	// topic, or pick a topic and let the type follow from it. Both are here;
	// "By topic" is the one that needs no type knowledge.
	var addDialog *dialog.ConfirmDialog
	addDisplay := func() {
		kindLabels := make([]string, 0, len(displayKinds))
		for _, kind := range displayKinds {
			kindLabels = append(kindLabels, kind.Label)
		}
		kindSelect := widget.NewSelect(kindLabels, nil)
		kindSelect.SetSelected(kindLabels[0])
		topicSelect := widget.NewSelect(nil, nil)
		kindHint := widget.NewLabel("")
		kindLoaded := false

		refreshKindTopics := func() {
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
				kindHint.SetText(fmt.Sprintf(tr("%d topic(s) of this type", "%d 个该类型的话题"), len(options)))
			} else if kindLoaded {
				kindHint.SetText(fmt.Sprintf(tr("no %s topic in the graph (press Refresh to look again)",
					"图里没有 %s 话题（可按 Refresh 再看一次）"),
					typeShort(typeName)))
			} else {
				kindHint.SetText(tr("looking for topics...", "正在查找话题..."))
			}
		}
		kindSelect.OnChanged = func(string) { refreshKindTopics() }
		refreshKindTopics()

		typeTab := container.NewVBox(
			widget.NewLabel(tr("Display type:", "显示类型:")),
			kindSelect,
			widget.NewLabel(tr("Topic:", "话题:")),
			topicSelect,
			kindHint,
		)

		// By topic: only the topics that have a display are offered, and the
		// hint says which display the selected topic will become.
		topicAny := widget.NewSelect(nil, nil)
		topicHint := widget.NewLabel("")
		refreshAnyTopics := func() {
			options := make([]string, 0, len(sh.graphNames))
			for _, name := range sh.graphNames {
				if _, ok := displayKindFor(sh.graphTypes[name]); ok {
					options = append(options, name)
				}
			}
			topicAny.Options = options
			topicAny.ClearSelected()
			topicAny.Refresh()
			if len(options) > 0 {
				topicAny.SetSelected(options[0])
			}
			topicHint.SetText(fmt.Sprintf(tr("%d topic(s) with a display", "%d 个可显示的话题"), len(options)))
		}
		topicAny.OnChanged = func(name string) {
			kind, ok := displayKindFor(sh.graphTypes[name])
			if !ok {
				topicHint.SetText(tr("no display for this type", "该类型没有对应的显示组件"))
				return
			}
			topicHint.SetText(fmt.Sprintf(tr("becomes a %s display", "将作为「%s」显示"), kind.Label))
		}
		anyTab := container.NewVBox(
			widget.NewLabel(tr("Topic:", "话题:")),
			topicAny,
			topicHint,
		)

		// A radio above the form, not a tab strip: tabs inside a dialog crowd
		// the labels and grow an overflow button when they do not fit, which
		// reads as noise. One radio decides which half of the stack shows.
		modeByType := tr("By display type", "按显示类型")
		modeByTopic := tr("By topic", "按话题")
		mode := modeByTopic
		if sh.addMode == "type" {
			mode = modeByType
		}
		modeSelect := widget.NewRadioGroup([]string{modeByType, modeByTopic},
			func(v string) {
				mode = v
				if v == modeByType {
					sh.addMode = "type"
					typeTab.Show()
					anyTab.Hide()
				} else {
					sh.addMode = "topic"
					typeTab.Hide()
					anyTab.Show()
				}
				if sh.autoSave != nil {
					sh.autoSave()
				}
			})
		modeSelect.Horizontal = true
		modeSelect.SetSelected(mode)
		if mode == modeByType {
			typeTab.Show()
			anyTab.Hide()
		} else {
			typeTab.Hide()
			anyTab.Show()
		}
		body := container.NewStack(typeTab, anyTab)

		// The dialog is modal, which greys out the window behind it - so the
		// status-bar diagnostics button cannot be reached while this is open.
		// A second one belongs here for exactly that reason.
		dialogCopy := widget.NewButtonWithIcon(tr("Copy diagnostics", "复制诊断信息"),
			theme.ContentCopyIcon(), func() { sh.copyDiagnostics() })
		addDialog = dialog.NewCustomConfirm(tr("Add a display", "添加显示"), tr("Add", "添加"), tr("Cancel", "取消"),
			container.NewVBox(modeSelect, body, dialogCopy),
			func(confirmed bool) {
				if !confirmed {
					return
				}
				var name, typeName string
				if mode == modeByTopic {
					name = topicAny.Selected
					typeName = sh.graphTypes[name]
				} else {
					name = topicSelect.Selected
					for _, kind := range displayKinds {
						if kind.Label == kindSelect.Selected {
							typeName = kind.Type
							break
						}
					}
				}
				if name == "" {
					sh.announce(tr("pick a topic first", "请先选择一个话题"), nil)
					return
				}
				kind, ok := displayKindFor(typeName)
				if !ok {
					sh.announce(fmt.Sprintf(tr("no display for %s", "%s 没有对应的显示组件"), emptyAs(typeName, "(unknown type)")), nil)
					return
				}
				if _, known := sh.cfg.Topics[name]; known {
					sh.announce(fmt.Sprintf(tr("%s is already shown", "%s 已经显示"), name), nil)
					return
				}
				sh.cfg.Topics[name] = TopicConfig{Type: typeName}
				sh.topicNames = append(sh.topicNames, name)
				sort.Strings(sh.topicNames)
				displayList.Refresh()
				if sh.feed != nil {
					sh.feed.resubscribe()
				}
				sh.announce(fmt.Sprintf(tr("added %s (%s)", "已添加 %s（%s）"), name, kind.Label), nil)
				if sh.autoSave != nil {
					sh.autoSave()
				}
			}, w)
		addDialog.Show()
		go func() {
			sh.fetchGraph(func(ok bool) {
				if !ok {
					kindHint.SetText("the sidecar did not answer yet - press Refresh")
					topicHint.SetText("the sidecar did not answer yet - press Refresh")
					return
				}
				kindLoaded = true
				refreshKindTopics()
				refreshAnyTopics()
				// Opening on "Map" when the graph has no map would look empty
				// even though it has a scan; jump to the first kind that does
				// have a topic.
				if len(topicSelect.Options) == 0 {
					for _, kind := range displayKinds {
						if len(topicsOfType(kind.Type)) > 0 {
							kindSelect.SetSelected(kind.Label)
							break
						}
					}
				}
				if len(topicAny.Options) > 0 {
					topicAny.SetSelected(topicAny.Options[0])
				}
			})
		}()
	}

	displays := container.NewVBox(
		displayList,
		container.NewHBox(
			widget.NewButtonWithIcon(tr("Add", "添加"), theme.ContentAddIcon(), addDisplay),
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
		viewStatus.SetText(tr("applied", "已应用"))
		sh.announce("view applied", nil)
		if sh.autoSave != nil {
			sh.autoSave()
		}
	}
	viewPanel := container.NewVBox(
		widget.NewLabel(tr("Fixed frame:", "固定坐标系:")),
		frameEntry,
		widget.NewLabel(tr("Grid cell (m):", "网格大小（米）:")),
		gridEntry,
		widget.NewLabel(tr("Background:", "背景色:")),
		container.NewBorder(nil, nil, nil,
			widget.NewButton(tr("Pick...", "选颜色..."), func() {
				sh.pickColor(backgroundEntry.Text, func(hex string) {
					backgroundEntry.SetText(hex)
					applyView()
				})
			}), backgroundEntry),
		container.NewHBox(
			widget.NewButton(tr("Apply", "应用"), applyView),
			widget.NewButton(tr("Fit view", "适应窗口"), func() {
				v.Fit()
				updateFooter()
			}),
		),
		viewStatus,
	)

	// The connection section answers the first question a missing display
	// raises: does the sidecar see the same graph this window does? The
	// namespace is the usual answer when it does not.
	namespace := os.Getenv("NAV2_SHM_NAMESPACE")
	if namespace == "" {
		namespace = "(default)"
	}
	graphLabel := widget.NewLabel(tr("graph:   - topics", "话题图:   - 个话题"))
	sh.graphLabel = graphLabel
	connection := container.NewVBox(
		widget.NewLabel("shared memory namespace: "+namespace),
		widget.NewLabel(tr("agent:  see the status bar", "agent:  见状态栏")),
		graphLabel,
	)

	// --- panel ------------------------------------------------------------
	// rviz's order: the displays are at the top, the properties of the
	// selected display right below them, then the other sections.
	accordion := widget.NewAccordion(
		widget.NewAccordionItem(tr("Displays", "显示"), displays),
		widget.NewAccordionItem(tr("Properties", "属性"), properties),
		widget.NewAccordionItem(tr("View", "视图"), viewPanel),
		widget.NewAccordionItem(tr("Connection", "连接"), connection),
	)
	accordion.Open(0)
	accordion.Open(1)
	sh.accordion = accordion

	// The panel is on the left and the viewport fills the rest, the way
	// rviz has it. Folding the panel leaves a thin rail with one button, so
	// the map gets the window without losing the way back.
	var toggle func()
	hideButton := widget.NewButtonWithIcon("", theme.NavigateBackIcon(), func() { toggle() })
	expandButton := widget.NewButtonWithIcon("", theme.NavigateNextIcon(), func() { toggle() })
	panel := container.NewBorder(
		container.NewHBox(layout.NewSpacer(), hideButton), nil, nil, nil, accordion)
	rail := container.NewVBox(expandButton)

	split := container.NewHSplit(panel, v)
	sh.split = split
	sh.setViewOffset(sh.savedOffset)

	toggle = func() {
		if sh.collapsed {
			split.Leading = panel
			sh.setViewOffset(sh.savedOffset)
			sh.collapsed = false
		} else {
			sh.savedOffset = sh.viewOffset()
			split.Leading = rail
			// The rail is a fixed number of pixels, which as a fraction
			// depends on how wide the window is right now.
			offset := 0.03
			if canvas := sh.win.Canvas(); canvas != nil && canvas.Size().Width > 0 {
				offset = 30 / float64(canvas.Size().Width)
			}
			if offset < 0.01 {
				offset = 0.01
			}
			if offset > 0.12 {
				offset = 0.12
			}
			split.SetOffset(offset)
			sh.collapsed = true
		}
		split.Refresh()
	}
	sh.togglePanel = toggle
	togglePanel := sh.togglePanel

	// --- config: load / save ----------------------------------------------
	// `apply` re-reads everything that is derived from the config: the
	// viewport's view settings, the display list, the window size, the splitter
	// position, and the footer (the zoom limits may have changed).
	sh.notify = func(what string, err error) {
		if err != nil {
			statusMsg.SetText(fmt.Sprintf(tr("%s failed: %v", "%s失败: %v"), what, err))
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
	// autoSave is the same write without the status line: the memory features
	// call it on every change, and a status message for each one would drown
	// out everything else.
	sh.autoSave = func() {
		if err := writeConfig(sh.path, sh.snapshot()); err != nil {
			sh.announce(tr("could not remember the workspace", "无法记住当前工作区"), err)
		}
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
			sh.notify(tr("loaded", "已加载"), err)
			if err == nil {
				v.Fit()
				updateFooter()
			}
		}),
		widget.NewButtonWithIcon("", theme.DocumentSaveIcon(), func() {
			sh.notify(tr("saved", "已保存"), sh.save())
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
			sh.notify(tr("loaded", "已加载"), err)
			if err == nil {
				v.Fit()
			}
		}
	})

	// Ctrl+S / Ctrl+O. A visualiser is used with the pointer in the viewport,
	// so both config paths need a shortcut as well as a toolbar button.
	w.Canvas().AddShortcut(
		&desktop.CustomShortcut{KeyName: fyne.KeyS, Modifier: fyne.KeyModifierControl},
		func(fyne.Shortcut) { sh.notify(tr("saved", "已保存"), sh.save()) },
	)
	w.Canvas().AddShortcut(
		&desktop.CustomShortcut{KeyName: fyne.KeyO, Modifier: fyne.KeyModifierControl},
		func(fyne.Shortcut) {
			err := sh.reload()
			sh.notify(tr("loaded", "已加载"), err)
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
		toolLabel.SetText(tr("tool: ", "工具: ") + toolName(v.Tool()))
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

	// Re-select the display that was open when the window last closed.
	if name := sh.cfg.UI.SelectedDisplay; name != "" {
		for i, entry := range sh.topicNames {
			if entry == name {
				displayList.Select(i)
				break
			}
		}
	}

	sh.feed = newFeedController(v, scene, func() Config { return sh.cfg }, sidecarLabel, frameLabel)
	sh.feed.onConnected = func() {
		go func() { sh.fetchGraph(nil) }()
	}
	sh.feed.start()
	w.SetCloseIntercept(func() {
		// Closing is the last chance to remember the workspace.
		if sh.save != nil {
			_ = sh.save()
		}
		sh.feed.close()
		w.Close()
	})
	return sh
}

// pickColor opens the colour wheel with the current colour in it. The
// callback gets "#rrggbb", which is the only form the configuration holds.
func (s *shell) pickColor(current string, apply func(string)) {
	initial := parseColor(current, theme.BackgroundColor())
	picker := dialog.NewColorPicker(tr("Pick a colour", "选择颜色"),
		tr("Colour", "颜色"), func(c color.Color) {
			rgba := color.RGBAModel.Convert(c).(color.RGBA)
			apply(fmt.Sprintf("#%02x%02x%02x", rgba.R, rgba.G, rgba.B))
		}, s.win)
	picker.Advanced = true
	picker.SetColor(initial)
	picker.Show()
}

// copyDiagnostics puts the diagnostics text on the clipboard.
func (s *shell) copyDiagnostics() {
	text := s.diagnosticsText()
	s.win.Clipboard().SetContent(text)
	s.announce(fmt.Sprintf(tr("diagnostics copied (%d bytes)", "诊断信息已复制（%d 字节）"), len(text)), nil)
}

// diagnosticsText is what "Copy diagnostics" puts on the clipboard: the facts
// a person cannot read off the window when a display does not appear.
func (s *shell) diagnosticsText() string {
	var b strings.Builder
	b.WriteString("mini-ros2 viz diagnostics\n")
	b.WriteString("time: " + time.Now().Format(time.RFC3339) + "\n")
	b.WriteString("config: " + s.path + "\n")
	b.WriteString("namespace: " + namespaceOf() + "\n")
	b.WriteString("agent: " + envOr("ROS2_BRIDGE_AGENT", "(unset)") + "\n")
	fmt.Fprintf(&b, "fixed frame: %s  zoom: %.2f px/m  center: %.2f, %.2f\n",
		fixedFrameOf(&s.cfg), s.view.Zoom(), s.view.cx, s.view.cy)

	fmt.Fprintf(&b, "\ngraph (%d topics):\n", len(s.graphNames))
	if len(s.graphNames) == 0 {
		b.WriteString("  (empty - the sidecar sees no topics)\n")
	}
	for _, name := range s.graphNames {
		mark := " "
		if _, added := s.cfg.Topics[name]; added {
			mark = "*"
		}
		fmt.Fprintf(&b, " %s %-34s %s\n", mark, name, s.graphTypes[name])
	}
	b.WriteString("  (* = has a display)\n")

	fmt.Fprintf(&b, "\ndisplays (%d):\n", len(s.topicNames))
	if len(s.topicNames) == 0 {
		b.WriteString("  (none added)\n")
	}
	for _, name := range s.topicNames {
		topic := s.cfg.Topics[name]
		fmt.Fprintf(&b,
			"  %-34s %-42s enabled=%v history=%d qos=%s/%s/%d color=%q size=%g alpha=%g\n",
			name, topic.Type, topic.enabled(), topic.History,
			emptyAs(topic.QOS.Reliability, "default"),
			emptyAs(topic.QOS.Durability, "default"),
			topic.QOS.Depth, topic.Color, topic.PointSize, topic.Alpha)
	}

	if s.feed != nil {
		snapshot := s.feed.scene.Snapshot()
		fmt.Fprintf(&b,
			"\nscene: maps=%d costmaps=%d clouds=%d scans=%d paths=%d poses=%d odom=%d marker_topics=%d\n",
			len(snapshot.Maps), len(snapshot.Costmaps), len(snapshot.Clouds),
			len(snapshot.Scans), len(snapshot.Paths), len(snapshot.Poses),
			len(snapshot.Odometry), len(snapshot.Markers))
		for name, layer := range snapshot.Scans {
			if layer != nil {
				fmt.Fprintf(&b, "  scan  %-30s points=%d stamp=%d frame=%q\n",
					name, len(layer.Points)/2, layer.StampNS, layer.Frame)
			}
		}
		for name, layer := range snapshot.Clouds {
			if layer != nil {
				fmt.Fprintf(&b, "  cloud %-30s points=%d stamp=%d frame=%q\n",
					name, len(layer.Points)/2, layer.StampNS, layer.Frame)
			}
		}
		for name, layer := range snapshot.Maps {
			if layer != nil {
				fmt.Fprintf(&b, "  map   %-30s %dx%d stamp=%d frame=%q\n",
					name, layer.Width, layer.Height, layer.StampNS, layer.Frame)
			}
		}
		for topic, message := range snapshot.Errors {
			fmt.Fprintf(&b, "  error %-30s %s\n", topic, message)
		}

		b.WriteString("\nrates (Hz):\n")
		rates := s.feed.rateSnapshot()
		if len(rates) == 0 {
			b.WriteString("  (no frames yet)\n")
		}
		names := make([]string, 0, len(rates))
		for name := range rates {
			names = append(names, name)
		}
		sort.Strings(names)
		for _, name := range names {
			fmt.Fprintf(&b, "  %-34s %d\n", name, rates[name])
		}

		if log := s.feed.lastLog(); log != "" {
			if len(snapshot.Frames) > 0 {
				b.WriteString("\ntf frames: " + strings.Join(snapshot.Frames, ", ") + "\n")
			}
			b.WriteString("\nsidecar log (tail):\n")
			b.WriteString(log)
			if !strings.HasSuffix(log, "\n") {
				b.WriteString("\n")
			}
		}
	}
	return b.String()
}

func namespaceOf() string {
	if ns := os.Getenv("NAV2_SHM_NAMESPACE"); ns != "" {
		return ns
	}
	return "(default)"
}

func envOr(name, fallback string) string {
	if value := os.Getenv(name); value != "" {
		return value
	}
	return fallback
}

func emptyAs(value, fallback string) string {
	if value == "" {
		return fallback
	}
	return value
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
	return "#ffffff"
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
				s.announce(tr("topic list failed", "读取话题列表失败"), err)
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
		if s.graphLabel != nil {
			s.graphLabel.SetText(fmt.Sprintf(tr("graph:   %d topic(s)", "话题图:   %d 个话题"), len(names)))
			s.graphLabel.Refresh()
		}
		if after != nil {
			after(true)
		} else {
			s.announce(fmt.Sprintf(tr("graph: %d topic(s)", "话题图: %d 个话题"), len(names)), nil)
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

	// The look of the view and the Add dialog's tab are part of "where I was",
	// not of the workspace file, so they ride along in the same document.
	view := cfg.Views["view"]
	view.CenterX, view.CenterY = s.view.Center()
	view.Scale = s.view.Zoom()
	cfg.Views["view"] = view
	cfg.UI.AddMode = s.addMode
	cfg.UI.SelectedDisplay = s.selectedDisplay
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
	if s.autoSave != nil {
		s.autoSave()
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
