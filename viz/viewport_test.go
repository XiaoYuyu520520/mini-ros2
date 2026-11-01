package main

import (
	"math"
	"testing"

	"fyne.io/fyne/v2"
	"fyne.io/fyne/v2/driver/desktop"
)

// The tool turns a click into a position and a drag into a heading. The
// viewport's scale is 40 px/m and its centre is the world origin, so the
// numbers below are exact.
func TestToolClickAndDrag(t *testing.T) {
	cfg := defaultConfig()
	v := newViewport(&cfg)
	v.Resize(fyne.NewSize(800, 600))

	type placed struct {
		mode toolMode
		x    float64
		y    float64
		yaw  float64
	}
	var got placed
	v.onPlace = func(mode toolMode, x, y, yaw float64) {
		got = placed{mode, x, y, yaw}
	}

	if v.setTool(toolGoal) != toolGoal {
		t.Fatal("the goal tool did not become active")
	}
	if v.Tool() != toolGoal {
		t.Fatal("the tool state was not recorded")
	}

	// Click at the centre: the position is the origin and there is no heading.
	v.MouseDown(&desktop.MouseEvent{
		Button:     desktop.MouseButtonPrimary,
		PointEvent: fyne.PointEvent{Position: fyne.NewPos(400, 300)},
	})
	v.MouseUp(&desktop.MouseEvent{
		Button:     desktop.MouseButtonPrimary,
		PointEvent: fyne.PointEvent{Position: fyne.NewPos(400, 300)},
	})
	if got.mode != toolGoal || got.x != 0 || got.y != 0 || got.yaw != 0 {
		t.Fatalf("click gave %+v", got)
	}

	// Drag to the right: the heading is zero, and the position is where the
	// button went down.
	v.MouseDown(&desktop.MouseEvent{
		Button:     desktop.MouseButtonPrimary,
		PointEvent: fyne.PointEvent{Position: fyne.NewPos(300, 300)},
	})
	v.MouseUp(&desktop.MouseEvent{
		Button:     desktop.MouseButtonPrimary,
		PointEvent: fyne.PointEvent{Position: fyne.NewPos(500, 300)},
	})
	if math.Abs(got.x-(-2.5)) > 1e-6 || math.Abs(got.y) > 1e-6 {
		t.Fatalf("drag start gave %+v, want x=-2.5", got)
	}
	if math.Abs(got.yaw) > 1e-6 {
		t.Fatalf("drag right gave yaw %v, want 0", got.yaw)
	}

	// Drag upwards on the screen: world y grows up, so the heading is a
	// quarter turn.
	v.MouseDown(&desktop.MouseEvent{
		Button:     desktop.MouseButtonPrimary,
		PointEvent: fyne.PointEvent{Position: fyne.NewPos(400, 400)},
	})
	v.MouseUp(&desktop.MouseEvent{
		Button:     desktop.MouseButtonPrimary,
		PointEvent: fyne.PointEvent{Position: fyne.NewPos(400, 200)},
	})
	if math.Abs(got.yaw-math.Pi/2) > 1e-6 {
		t.Fatalf("drag up gave yaw %v, want %v", got.yaw, math.Pi/2)
	}
	// The position is where the button went down: 100 px below the centre is
	// 2.5 m below the origin.
	if math.Abs(got.y-(-2.5)) > 1e-6 {
		t.Fatalf("drag up gave y %v, want -2.5", got.y)
	}
}

// Selecting the active tool again turns it off, and Escape clears it.
func TestToolToggles(t *testing.T) {
	cfg := defaultConfig()
	v := newViewport(&cfg)
	v.Resize(fyne.NewSize(400, 300))

	if v.setTool(toolPose) != toolPose {
		t.Fatal("the pose tool did not become active")
	}
	if v.setTool(toolPose) != toolNone {
		t.Fatal("the pose tool did not toggle off")
	}
	v.setTool(toolPose)
	v.cancelTool()
	if v.Tool() != toolNone {
		t.Fatal("cancel did not clear the tool")
	}
}

// With no tool selected, a drag pans the view; it must not publish anything.
func TestDragWithoutToolPans(t *testing.T) {
	cfg := defaultConfig()
	v := newViewport(&cfg)
	v.Resize(fyne.NewSize(800, 600))
	published := false
	v.onPlace = func(toolMode, float64, float64, float64) { published = true }

	v.Dragged(&fyne.DragEvent{
		PointEvent: fyne.PointEvent{Position: fyne.NewPos(440, 300)},
		Dragged:    fyne.Delta{DX: 40, DY: 0},
	})
	if published {
		t.Fatal("a drag published a pose")
	}
	if v.cx != -1 {
		t.Fatalf("the view did not pan: cx %v", v.cx)
	}
}

// The Select tool reports the clicked world point, and the Publish Point tool
// reports it too - they are the two rviz tools that work on a single click.
func TestSelectAndPublishPointTools(t *testing.T) {
	cfg := defaultConfig()
	v := newViewport(&cfg)
	v.Resize(fyne.NewSize(800, 600))

	var selectedX, selectedY float64
	selected := false
	v.onSelect = func(x, y float64) { selectedX, selectedY, selected = x, y, true }
	var publishedX, publishedY float64
	published := false
	v.onPublishPoint = func(x, y float64) { publishedX, publishedY, published = x, y, true }

	click := func() {
		v.MouseDown(&desktop.MouseEvent{
			Button:     desktop.MouseButtonPrimary,
			PointEvent: fyne.PointEvent{Position: fyne.NewPos(500, 300)},
		})
		v.MouseUp(&desktop.MouseEvent{
			Button:     desktop.MouseButtonPrimary,
			PointEvent: fyne.PointEvent{Position: fyne.NewPos(500, 300)},
		})
	}

	v.setTool(toolSelect)
	click()
	if !selected {
		t.Fatal("the Select tool did not report the click")
	}
	// The scale is 40 px/m and the centre is the origin: 100 px right of the
	// centre is 2.5 m.
	if math.Abs(selectedX-2.5) > 1e-6 || math.Abs(selectedY) > 1e-6 {
		t.Fatalf("Select reported (%v,%v), want (2.5,0)", selectedX, selectedY)
	}
	if published {
		t.Fatal("the Select tool published a point")
	}

	v.setTool(toolPoint)
	click()
	if !published {
		t.Fatal("the Publish Point tool did not report the click")
	}
	if math.Abs(publishedX-2.5) > 1e-6 || math.Abs(publishedY) > 1e-6 {
		t.Fatalf("Publish Point reported (%v,%v), want (2.5,0)", publishedX, publishedY)
	}
}

// A drag with the Select tool is a camera drag, not a pick: the end of a drag
// must not select anything.
func TestDragWithSelectToolDoesNotSelect(t *testing.T) {
	cfg := defaultConfig()
	v := newViewport(&cfg)
	v.Resize(fyne.NewSize(800, 600))
	selected := false
	v.onSelect = func(float64, float64) { selected = true }

	v.setTool(toolSelect)
	v.MouseDown(&desktop.MouseEvent{
		Button:     desktop.MouseButtonPrimary,
		PointEvent: fyne.PointEvent{Position: fyne.NewPos(400, 300)},
	})
	v.MouseUp(&desktop.MouseEvent{
		Button:     desktop.MouseButtonPrimary,
		PointEvent: fyne.PointEvent{Position: fyne.NewPos(600, 300)},
	})
	if selected {
		t.Fatal("the end of a drag selected something")
	}
}

// History Length keeps past sweeps: with a history of three, three messages
// are drawn, and the newest one is the last.
func TestHistoryKeepsPastScans(t *testing.T) {
	cfg := defaultConfig()
	v := newViewport(&cfg)
	v.Resize(fyne.NewSize(800, 600))

	layer := &ScanLayer{Points: []float64{1, 0}, StampNS: 1}
	first := v.scanTrail("/scan", layer, 3)
	if len(first) != 1 {
		t.Fatalf("the first message made %d trails", len(first))
	}

	second := &ScanLayer{Points: []float64{2, 0}, StampNS: 2}
	trail := v.scanTrail("/scan", second, 3)
	if len(trail) != 2 {
		t.Fatalf("the second message made %d trails", len(trail))
	}
	if trail[0][0] != 1 || trail[1][0] != 2 {
		t.Fatalf("the trail is not in order: %v", trail)
	}

	// The same message twice must not grow the trail.
	if got := v.scanTrail("/scan", second, 3); len(got) != 2 {
		t.Fatalf("a repeated message made %d trails", len(got))
	}

	// A history of one is only the newest sweep.
	if got := v.scanTrail("/scan", second, 1); len(got) != 1 {
		t.Fatalf("a history of one kept %d trails", len(got))
	}
}
