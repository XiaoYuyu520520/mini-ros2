package main

import (
	"fmt"
	"image"
	"image/color"
	"image/draw"
	"math"

	"fyne.io/fyne/v2"
	"fyne.io/fyne/v2/canvas"
	"fyne.io/fyne/v2/widget"
)

// viewport is the 2D map view: a world rectangle drawn into a bitmap, with
// pan (drag) and zoom (wheel, anchored at the pointer). It is a custom widget
// rather than a container of canvas objects because the map is a grid that
// changes every frame; rasterising it once per frame is cheaper than moving
// tens of thousands of lines.
//
// The bitmap is generated at the *device* pixel size (logical size times the
// canvas scale) and stretched over the logical widget. That is what keeps the
// grid crisp on a high-DPI monitor instead of being upscaled by the theme.
type viewport struct {
	widget.BaseWidget

	img *canvas.Image

	// scale is logical pixels per metre.
	scale      float64
	minScale   float64
	maxScale   float64
	cx, cy     float64 // world point at the centre of the widget
	bg         color.Color
	gridCellM  float64
	showPan    bool
	cursorText string

	onCursor func(x, y float64)

	dragging bool
	lastPos  fyne.Position
}

func newViewport(cfg *Config) *viewport {
	v := &viewport{
		img:       canvas.NewImageFromImage(image.NewRGBA(image.Rect(0, 0, 1, 1))),
		scale:     40,
		minScale:  1,
		maxScale:  4000,
		bg:        parseColor("#1e1e1e", color.RGBA{30, 30, 30, 255}),
		gridCellM: 1,
		showPan:   true,
	}
	if view, ok := cfg.Views["view"]; ok {
		v.bg = parseColor(view.Background, v.bg)
		if view.GridCellM > 0 {
			v.gridCellM = view.GridCellM
		}
		if view.Zoom.Min > 0 {
			v.minScale = view.Zoom.Min
		}
		if view.Zoom.Max > 0 {
			v.maxScale = view.Zoom.Max
		}
		v.showPan = view.Pan
	}
	v.img.FillMode = canvas.ImageFillStretch
	v.img.ScaleMode = canvas.ImageScaleSmooth
	v.ExtendBaseWidget(v)
	return v
}

func (v *viewport) CreateRenderer() fyne.WidgetRenderer {
	return &viewportRenderer{v: v}
}

type viewportRenderer struct {
	v *viewport
}

func (r *viewportRenderer) Layout(size fyne.Size) {
	r.v.img.Resize(size)
}
func (r *viewportRenderer) MinSize() fyne.Size { return fyne.NewSize(160, 120) }
func (r *viewportRenderer) Refresh() {
	r.v.render()
	r.v.img.Refresh()
}
func (r *viewportRenderer) Objects() []fyne.CanvasObject { return []fyne.CanvasObject{r.v.img} }
func (r *viewportRenderer) Destroy()                     {}

// Resize re-rasterises: BaseWidget.Resize resizes the image object, and the
// pixels behind it have to follow or the map would be stretched.
func (v *viewport) Resize(size fyne.Size) {
	v.BaseWidget.Resize(size)
	v.render()
}

// render draws the grid into a bitmap sized for the screen's current DPI.
func (v *viewport) render() {
	size := v.Size()
	if size.Width < 1 || size.Height < 1 {
		return
	}

	scale := v.dpiScale()
	w := int(math.Round(float64(size.Width) * scale))
	h := int(math.Round(float64(size.Height) * scale))
	if w < 1 || h < 1 {
		return
	}

	img := image.NewRGBA(image.Rect(0, 0, w, h))
	draw.Draw(img, img.Bounds(), &image.Uniform{C: v.bg}, image.Point{}, draw.Src)

	// Device pixels per metre.
	ppm := v.scale * scale
	if ppm <= 0 {
		return
	}

	// World bounds visible in this bitmap.
	halfW := float64(w) / (2 * ppm)
	halfH := float64(h) / (2 * ppm)
	minX, maxX := v.cx-halfW, v.cx+halfW
	minY, maxY := v.cy-halfH, v.cy+halfH

	line := color.RGBA{52, 52, 52, 255}
	axisX := color.RGBA{120, 40, 40, 255}
	axisY := color.RGBA{40, 110, 40, 255}

	grid := v.gridCellM
	// Thin the grid out when a cell would be smaller than a few pixels, so a
	// zoomed-out view does not turn into a solid block.
	for grid*ppm < 6 {
		grid *= 5
	}

	// Vertical lines at world x = k*grid.
	first := math.Ceil(minX/grid) * grid
	for x := first; x <= maxX; x += grid {
		px := int(math.Round((x - minX) * ppm))
		c := line
		if math.Abs(x) < grid*0.001 {
			c = axisY
		}
		drawVLine(img, px, c)
	}
	first = math.Ceil(minY/grid) * grid
	for y := first; y <= maxY; y += grid {
		py := int(math.Round((maxY - y) * ppm)) // world y up, bitmap y down
		c := line
		if math.Abs(y) < grid*0.001 {
			c = axisX
		}
		drawHLine(img, py, c)
	}

	// A faint crosshair at the world origin, so "empty" still has a reference.
	originX := int(math.Round((0 - minX) * ppm))
	originY := int(math.Round((maxY - 0) * ppm))
	drawPlus(img, originX, originY, color.RGBA{90, 90, 140, 255})

	v.img.Image = img
}

func (v *viewport) dpiScale() float64 {
	if app := fyne.CurrentApp(); app != nil {
		if c := app.Driver().CanvasForObject(v); c != nil {
			s := float64(c.Scale())
			if s > 0 {
				return s
			}
		}
	}
	return 1
}

// --- interaction -----------------------------------------------------------

func (v *viewport) Scrolled(e *fyne.ScrollEvent) {
	if e.Scrolled.DY == 0 {
		return
	}
	factor := 1.15
	if e.Scrolled.DY < 0 {
		factor = 1 / factor
	}
	v.zoomAt(e.Position, factor)
}

func (v *viewport) zoomAt(pos fyne.Position, factor float64) {
	size := v.Size()
	old := v.scale
	next := clamp(old*factor, v.minScale, v.maxScale)
	if next == old {
		return
	}
	// Keep the world point under the pointer fixed.
	wx := v.cx + (float64(pos.X)-float64(size.Width)/2)/old
	wy := v.cy - (float64(pos.Y)-float64(size.Height)/2)/old
	v.scale = next
	v.cx = wx - (float64(pos.X)-float64(size.Width)/2)/next
	v.cy = wy + (float64(pos.Y)-float64(size.Height)/2)/next
	v.render()
	v.img.Refresh()
}

func (v *viewport) Dragged(e *fyne.DragEvent) {
	if !v.showPan {
		return
	}
	v.cx -= float64(e.Dragged.DX) / v.scale
	v.cy += float64(e.Dragged.DY) / v.scale
	v.render()
	v.img.Refresh()
}

func (v *viewport) DragEnd() {}

func (v *viewport) MouseMoved(e *fyne.PointEvent) {
	v.updateCursor(e.Position)
}

// worldAt converts a widget position to world coordinates.
func (v *viewport) worldAt(pos fyne.Position) (float64, float64) {
	size := v.Size()
	x := v.cx + (float64(pos.X)-float64(size.Width)/2)/v.scale
	y := v.cy - (float64(pos.Y)-float64(size.Height)/2)/v.scale
	return x, y
}

// updateCursor reports the world position (the status bar shows it, the same
// way rviz does).
func (v *viewport) updateCursor(pos fyne.Position) {
	if v.onCursor != nil {
		x, y := v.worldAt(pos)
		v.onCursor(x, y)
	}
}

// Fit puts the origin in the middle at a moderate zoom.
func (v *viewport) Fit() {
	v.cx, v.cy = 0, 0
	v.scale = clamp(40, v.minScale, v.maxScale)
	v.render()
	v.img.Refresh()
}

func (v *viewport) Zoom() float64 { return v.scale }

// --- drawing helpers -------------------------------------------------------

func drawVLine(img *image.RGBA, x int, c color.Color) {
	if x < 0 || x >= img.Bounds().Dx() {
		return
	}
	for y := 0; y < img.Bounds().Dy(); y++ {
		img.Set(x, y, c)
	}
}

func drawHLine(img *image.RGBA, y int, c color.Color) {
	if y < 0 || y >= img.Bounds().Dy() {
		return
	}
	for x := 0; x < img.Bounds().Dx(); x++ {
		img.Set(x, y, c)
	}
}

func drawPlus(img *image.RGBA, x, y int, c color.Color) {
	for d := -6; d <= 6; d++ {
		if x+d >= 0 && x+d < img.Bounds().Dx() {
			img.Set(x+d, y, c)
		}
		if y+d >= 0 && y+d < img.Bounds().Dy() {
			img.Set(x, y+d, c)
		}
	}
}

func clamp(v, lo, hi float64) float64 {
	if v < lo {
		return lo
	}
	if v > hi {
		return hi
	}
	return v
}

func parseColor(s string, fallback color.Color) color.Color {
	if len(s) == 7 && s[0] == '#' {
		var r, g, b uint8
		if _, err := fmt.Sscanf(s, "#%02x%02x%02x", &r, &g, &b); err == nil {
			return color.RGBA{R: r, G: g, B: b, A: 255}
		}
	}
	return fallback
}
