package main

import (
	"strings"
	"testing"

	"fyne.io/fyne/v2"
	"fyne.io/fyne/v2/test"
	"fyne.io/fyne/v2/widget"
)

// findButton digs the clickable row out of a display row.
func findButton(objects []fyne.CanvasObject) *widget.Button {
	for _, object := range objects {
		if button, ok := object.(*widget.Button); ok {
			return button
		}
		if container, ok := object.(*fyne.Container); ok {
			if button := findButton(container.Objects); button != nil {
				return button
			}
		}
	}
	return nil
}

// Clicking a display row must select that display and open the properties:
// the whole point of the row is to reach the colour and size of that topic.
func TestClickingADisplaySelectsIt(t *testing.T) {
	a := test.NewApp()
	defer a.Quit()
	w := test.NewWindow(nil)

	cfg := defaultConfig()
	sh := buildUI(a, w, &cfg, "test")

	var scanButton *widget.Button
	for _, row := range sh.displayRows.Objects {
		container, ok := row.(*fyne.Container)
		if !ok {
			continue
		}
		button := findButton(container.Objects)
		if button != nil && strings.Contains(button.Text, "/scan") {
			scanButton = button
			break
		}
	}
	if scanButton == nil {
		t.Fatalf("no /scan row in %d rows", len(sh.displayRows.Objects))
	}

	test.Tap(scanButton)
	if sh.selectedDisplay != "/scan" {
		t.Fatalf("clicking the row selected %q, want /scan", sh.selectedDisplay)
	}
	if len(sh.accordion.Items) < 2 || !sh.accordion.Items[1].Open {
		t.Fatal("the properties section was not opened")
	}
}