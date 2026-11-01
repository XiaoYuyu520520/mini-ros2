// mini-ros2 viz - a Windows/Linux visualiser for the shared-memory ROS2 graph.
//
// This is the shell: a 2D map viewport with pan/zoom, a collapsible side
// panel, and a JSON config. The sidecar that feeds it real topics does not
// exist yet; the point of this binary is that the frame can be dragged,
// resized, scaled on a high-DPI monitor and collapsed before any data plumbing
// is written.
//
//	go run .                      # config.json next to the exe / cwd
//	go run . -config other.json
//
// Build (Windows, MSYS2 UCRT64 gcc for cgo):
//
//	set PATH=D:\...\MSYS2\ucrt64\bin;%PATH%
//	go build -ldflags "-H windowsgui" -o viz.exe .
package main

import (
	"flag"
	"log"

	"fyne.io/fyne/v2"
	"fyne.io/fyne/v2/app"
)

func main() {
	configPath := flag.String("config", "", "path to config.json (default: next to the exe, then cwd)")
	flag.Parse()

	cfg, path, err := loadConfig(*configPath)
	if err != nil {
		log.Printf("config: %v", err)
	}
	log.Printf("config: %s", path)

	a := app.NewWithID("mini-ros2.viz")
	a.Settings().SetTheme(uiTheme(cfg.UI.Language))

	w := a.NewWindow("mini-ros2 viz")
	w.Resize(fyne.NewSize(float32(cfg.Window.Width), float32(cfg.Window.Height)))
	w.SetPadded(true)
	buildUI(a, w, &cfg, path)
	if cfg.Window.Center {
		w.CenterOnScreen()
	}
	w.ShowAndRun()
}
