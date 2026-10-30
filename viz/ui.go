package main

import (
	"fmt"
	"sort"

	"fyne.io/fyne/v2"
	"fyne.io/fyne/v2/container"
	"fyne.io/fyne/v2/theme"
	"fyne.io/fyne/v2/widget"
)

// shell is the assembled window. It is a struct rather than a function's local
// state so the layout behaviour (collapse, fit) can be exercised by a test
// without a screen.
type shell struct {
	view         *viewport
	split        *container.Split
	accordion    *widget.Accordion
	topicList    *widget.List
	savedOffset  float64
	collapsed    bool
	togglePanel  func()
	updateFooter func()
}

// buildUI assembles the shell: a toolbar, a 2D viewport, a collapsible side
// panel of accordion sections, and a status bar. Nothing here talks to the
// sidecar yet - the point is the frame the displays will be dropped into.
func buildUI(a fyne.App, w fyne.Window, cfg *Config, cfgPath string) *shell {
	v := newViewport(cfg)

	// --- status bar -------------------------------------------------------
	zoomLabel := widget.NewLabel("zoom: -")
	cursorLabel := widget.NewLabel("x: -  y: -")
	dpiLabel := widget.NewLabel("dpi: -")
	v.onCursor = func(x, y float64) {
		cursorLabel.SetText(fmt.Sprintf("x: %+8.2f  y: %+8.2f", x, y))
	}

	status := container.NewHBox(
		widget.NewLabelWithStyle("ready", fyne.TextAlignLeading, fyne.TextStyle{Monospace: true}),
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
	// sidecar supplies real topics.
	topicNames := make([]string, 0, len(cfg.Topics))
	for name := range cfg.Topics {
		topicNames = append(topicNames, name)
	}
	sort.Strings(topicNames)

	topicList := widget.NewList(
		func() int { return len(topicNames) },
		func() fyne.CanvasObject { return widget.NewLabel("template") },
		func(i widget.ListItemID, o fyne.CanvasObject) {
			o.(*widget.Label).SetText(topicNames[i])
		},
	)

	displays := container.NewVBox(
		widget.NewLabel("Select a topic to add a display."),
		widget.NewLabel("The sidecar will supply the real list."),
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

	// --- split + collapse -------------------------------------------------
	split := container.NewHSplit(v, accordion)
	savedOffset := cfg.Layout.PanelOffset
	if savedOffset <= 0 || savedOffset >= 1 {
		savedOffset = 0.74
	}
	split.SetOffset(savedOffset)
	panelCollapsed := false

	updateFooter := func() {
		zoomLabel.SetText(fmt.Sprintf("zoom: %.1f px/m", v.Zoom()))
		dpiLabel.SetText(fmt.Sprintf("dpi: %.2fx", v.dpiScale()))
	}
	updateFooter()

	sh := &shell{view: v, split: split, accordion: accordion, topicList: topicList}
	sh.togglePanel = func() {
		if panelCollapsed {
			split.SetOffset(savedOffset)
			panelCollapsed = false
		} else {
			savedOffset = split.Offset
			split.SetOffset(0.999)
			panelCollapsed = true
		}
		sh.collapsed = panelCollapsed
		split.Refresh()
	}
	togglePanel := sh.togglePanel

	// --- toolbar ----------------------------------------------------------
	toolbar := widget.NewToolbar(
		widget.NewToolbarAction(theme.ViewRefreshIcon(), func() {
			v.Fit()
			updateFooter()
		}),
		widget.NewToolbarAction(theme.ContentAddIcon(), func() {
			name := fmt.Sprintf("/new/topic/%d", len(topicNames)+1)
			topicNames = append(topicNames, name)
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
		case fyne.KeyF8:
			v.Fit()
			updateFooter()
		case fyne.KeyF9:
			togglePanel()
		}
	})

	w.SetTitle(fmt.Sprintf("mini-ros2 viz — %s", cfgPath))
	sh.updateFooter = updateFooter
	return sh
}
