package main

import (
	"fmt"
	"image"
	"image/color"
	"image/draw"
	"math"

	"fyne.io/fyne/v2"
	"fyne.io/fyne/v2/canvas"
	"fyne.io/fyne/v2/driver/desktop"
	"fyne.io/fyne/v2/widget"
)

type toolMode int

const (
	toolNone toolMode = iota
	toolGoal
	toolPose
	// toolPan is rviz's "Move Camera": dragging moves the view.
	toolPan
	// toolSelect picks the display under the pointer.
	toolSelect
	// toolPoint publishes the clicked point, like rviz's "Publish Point".
	toolPoint
)

// toolName is the name the status bar shows for a tool.
func toolName(mode toolMode) string {
	switch mode {
	case toolGoal:
		return tr("2D Goal Pose", "2D 目标点")
	case toolPose:
		return tr("2D Pose Estimate", "2D 初始位姿")
	case toolPan:
		return tr("Move Camera", "移动视角")
	case toolSelect:
		return tr("Select", "选择")
	case toolPoint:
		return tr("Publish Point", "发布点")
	}
	return tr("none", "无")
}

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

	// scene is what the sidecar delivered; nil means "no sidecar yet" and the
	// viewport draws the grid alone.
	scene *Scene
	// style is the display configuration: how a map is coloured, how big a
	// scan point is.
	style DisplayConfig
	// topics are the per-topic overrides of that style.
	topics map[string]TopicConfig

	onCursor func(x, y float64)

	// Tools. A goal or an initial pose is a click (position) plus an optional
	// drag (heading); the drag is drawn as a preview until the button is
	// released.
	tool          toolMode
	toolDrag      *toolDrag
	onPlace       func(mode toolMode, x, y, yaw float64)
	onToolChanged func(mode toolMode)
	// onSelect is the "Select" tool: the shell resolves the clicked world
	// point to the display that owns it.
	onSelect func(x, y float64)
	// onPublishPoint is the "Publish Point" tool.
	onPublishPoint func(x, y float64)

	// press is where the button went down. The click tools need it to tell a
	// click from the end of a camera drag.
	press    fyne.Position
	pressSet bool
	// picked highlights the last point the Select tool resolved.
	picked   *[2]float64
	trails   map[string]*trail
	dragging bool
	lastPos  fyne.Position
}

// trail keeps the last messages of a scan or a cloud, so History Length draws
// a trail rather than only the newest sweep.
type trail struct {
	stamp  int64
	scans  [][]float64
	clouds []*PointCloudLayer
}

type toolDrag struct {
	mode           toolMode
	startX, startY float64
	endX, endY     float64
	active         bool
}

func newViewport(cfg *Config) *viewport {
	v := &viewport{
		img:       canvas.NewImageFromImage(image.NewRGBA(image.Rect(0, 0, 1, 1))),
		scale:     40,
		minScale:  1,
		maxScale:  4000,
		bg:        parseColor("#ffffff", color.RGBA{255, 255, 255, 255}),
		gridCellM: 1,
		showPan:   true,
		style:     cfg.Display,
		topics:    cfg.Topics,
		trails:    map[string]*trail{},
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

	// The grid and the axes have to work on a white background as well as on
	// a dark one: a fixed dark grey grid disappears on white, and a dark red
	// axis barely reads. The colours follow the background's brightness.
	light := backgroundIsLight(v.bg)
	line := color.RGBA{52, 52, 52, 255}
	axisX := color.RGBA{220, 50, 50, 255}
	axisY := color.RGBA{30, 150, 50, 255}
	if light {
		line = color.RGBA{205, 205, 205, 255}
	}

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
		drawVLine(img, px, line)
	}
	first = math.Ceil(minY/grid) * grid
	for y := first; y <= maxY; y += grid {
		py := int(math.Round((maxY - y) * ppm)) // world y up, bitmap y down
		drawHLine(img, py, line)
	}

	v.drawScene(img, ppm, w, h)

	// The two axes go on top of the scene: a map or a cloud covering the
	// origin must not hide the only fixed reference the grid has.
	axisWidth := 2
	if px := int(math.Round((0 - minX) * ppm)); px >= 0 && px < w {
		drawVLineW(img, px, axisY, axisWidth)
	}
	if py := int(math.Round((maxY - 0) * ppm)); py >= 0 && py < h {
		drawHLineW(img, py, axisX, axisWidth)
	}
	// A small crosshair at the origin, so "empty" still has a reference.
	originX := int(math.Round((0 - minX) * ppm))
	originY := int(math.Round((maxY - 0) * ppm))
	crosshair := color.RGBA{90, 90, 140, 255}
	if light {
		crosshair = color.RGBA{60, 60, 160, 255}
	}
	drawPlus(img, originX, originY, crosshair)

	v.drawToolPreview(img, ppm, w, h)
	v.drawPicked(img, ppm, w, h)

	v.img.Image = img
}

// drawToolPreview paints the arrow a tool would publish while the pointer is
// down. The start point is the position, the direction is the heading - the
// same convention rviz's 2D Goal Pose and 2D Pose Estimate use.
func (v *viewport) drawToolPreview(img *image.RGBA, ppm float64, w, h int) {
	if v.toolDrag == nil || !v.toolDrag.active {
		return
	}
	tint := color.RGBA{255, 140, 0, 255}
	if v.toolDrag.mode == toolPose {
		tint = color.RGBA{0, 200, 255, 255}
	}
	dx := v.toolDrag.endX - v.toolDrag.startX
	dy := v.toolDrag.endY - v.toolDrag.startY
	yaw := 0.0
	if math.Hypot(dx, dy) > 0.15 {
		yaw = math.Atan2(dy, dx)
	}
	drawLine(
		img,
		(v.toolDrag.startX-v.cx)*ppm+float64(w)/2,
		(v.cy-v.toolDrag.startY)*ppm+float64(h)/2,
		(v.toolDrag.endX-v.cx)*ppm+float64(w)/2,
		(v.cy-v.toolDrag.endY)*ppm+float64(h)/2,
		tint, 2, w, h)
	drawPose(img, v.toolDrag.endX, v.toolDrag.endY, yaw, ppm, v.cx, v.cy, w, h, tint, 14)
}

// drawPicked marks the world point the Select tool resolved: a ring plus a
// crosshair, so it is visible on a map as well as on a dark grid.
func (v *viewport) drawPicked(img *image.RGBA, ppm float64, w, h int) {
	if v.picked == nil {
		return
	}
	tint := color.RGBA{255, 220, 0, 255}
	sx := (v.picked[0]-v.cx)*ppm + float64(w)/2
	sy := (v.cy-v.picked[1])*ppm + float64(h)/2
	for d := 0; d < 9; d++ {
		angle := float64(d) * math.Pi / 4
		px := int(math.Round(sx + 9*math.Cos(angle)))
		py := int(math.Round(sy + 9*math.Sin(angle)))
		for oy := -1; oy <= 1; oy++ {
			for ox := -1; ox <= 1; ox++ {
				x, y := px+ox, py+oy
				if x >= 0 && y >= 0 && x < w && y < h {
					img.Set(x, y, tint)
				}
			}
		}
	}
}

// setTool selects a tool; selecting the active one turns it off.
func (v *viewport) setTool(mode toolMode) toolMode {
	if v.tool == mode {
		v.tool = toolNone
	} else {
		v.tool = mode
	}
	v.toolDrag = nil
	v.render()
	v.img.Refresh()
	if v.onToolChanged != nil {
		v.onToolChanged(v.tool)
	}
	return v.tool
}

func (v *viewport) Tool() toolMode { return v.tool }

func (v *viewport) cancelTool() {
	if v.tool == toolNone && v.toolDrag == nil {
		return
	}
	v.tool = toolNone
	v.toolDrag = nil
	v.render()
	v.img.Refresh()
	if v.onToolChanged != nil {
		v.onToolChanged(v.tool)
	}
}

func (v *viewport) MouseDown(e *desktop.MouseEvent) {
	if e.Button != desktop.MouseButtonPrimary {
		return
	}
	v.press = e.Position
	v.pressSet = true
	if v.tool != toolGoal && v.tool != toolPose {
		// The camera tools drag the view and the click tools only need the
		// press position; neither draws a preview.
		return
	}
	x, y := v.worldAt(e.Position)
	v.toolDrag = &toolDrag{
		mode: v.tool, startX: x, startY: y, endX: x, endY: y, active: true}
	v.render()
	v.img.Refresh()
}

func (v *viewport) MouseUp(e *desktop.MouseEvent) {
	if e.Button != desktop.MouseButtonPrimary {
		return
	}
	if v.toolDrag != nil && v.toolDrag.active {
		drag := v.toolDrag
		x, y := v.worldAt(e.Position)
		drag.endX, drag.endY = x, y
		dx := drag.endX - drag.startX
		dy := drag.endY - drag.startY
		yaw := 0.0
		if math.Hypot(dx, dy) > 0.15 {
			yaw = math.Atan2(dy, dx)
		}
		drag.active = false
		v.toolDrag = nil
		if v.onPlace != nil {
			v.onPlace(drag.mode, drag.startX, drag.startY, yaw)
		}
		v.render()
		v.img.Refresh()
		return
	}
	// The click tools only act on a click, not on the end of a camera drag.
	if !v.pressSet {
		return
	}
	moved := math.Hypot(float64(e.Position.X-v.press.X), float64(e.Position.Y-v.press.Y))
	v.pressSet = false
	if moved > 4 {
		return
	}
	x, y := v.worldAt(e.Position)
	switch v.tool {
	case toolSelect:
		if v.onSelect != nil {
			v.onSelect(x, y)
		}
	case toolPoint:
		if v.onPublishPoint != nil {
			v.onPublishPoint(x, y)
		}
	}
}

// drawScene paints the layers the sidecar delivered: the map first (it is the
// ground), then the scans, then the paths and the robot on top.
func (v *viewport) drawScene(img *image.RGBA, ppm float64, w, h int) {
	if v.scene == nil {
		return
	}
	snapshot := v.scene.Snapshot()

	for name, layer := range snapshot.Maps {
		if layer == nil {
			continue
		}
		topic := v.topics[name]
		alpha := v.style.OccupancyGrid.Alpha
		if topic.Alpha > 0 {
			alpha = topic.Alpha
		}
		if layer.Texture == nil {
			buildTexture(
				layer,
				rgbaColor(v.style.OccupancyGrid.FreeColor, color.RGBA{242, 242, 242, 255}),
				rgbaColor(v.style.OccupancyGrid.Occupied, color.RGBA{32, 32, 32, 255}),
				rgbaColor(v.style.OccupancyGrid.UnknownColor, color.RGBA{122, 122, 122, 255}),
				alpha)
		}
		drawMap(img, layer, ppm, v.cx, v.cy, w, h,
			layer.OriginX, layer.OriginY, layer.OriginYaw)
	}

	for name, layer := range snapshot.Costmaps {
		if layer == nil {
			continue
		}
		topic := v.topics[name]
		alpha := v.style.Costmap.Alpha
		if topic.Alpha > 0 {
			alpha = topic.Alpha
		}
		if layer.Texture == nil {
			buildCostmapTexture(
				layer,
				rgbaColor(v.style.Costmap.LowColor, color.RGBA{43, 108, 255, 255}),
				rgbaColor(v.style.Costmap.HighColor, color.RGBA{255, 64, 64, 255}),
				rgbaColor(v.style.Costmap.InscribedColor, color.RGBA{0, 229, 255, 255}),
				rgbaColor(v.style.Costmap.LethalColor, color.RGBA{16, 16, 16, 255}),
				rgbaColor(v.style.Costmap.UnknownColor, color.RGBA{122, 122, 122, 255}),
				alpha)
		}
		drawCostmap(img, layer, ppm, v.cx, v.cy, w, h)
	}

	for name, layer := range snapshot.Clouds {
		if layer == nil {
			continue
		}
		topic := v.topics[name]
		tint := rgbaColor(firstNonEmpty(topic.Color, v.style.PointCloud.Color),
			color.RGBA{0, 208, 255, 255})
		size := v.style.PointCloud.Size
		if topic.PointSize > 0 {
			size = topic.PointSize
		}
		if size <= 0 {
			size = 2
		}
		byHeight := v.style.PointCloud.ColorByHeight && topic.Color == ""
		// The history is drawn first, oldest first, so the newest sweep is
		// the bright one on top.
		past := v.cloudTrail(name, layer, topic.History)
		for i, previous := range past {
			fade := float64(i+1) / float64(len(past))
			drawCloud(img, previous, ppm, v.cx, v.cy, w, h, dimColor(tint, fade), size, byHeight,
				v.style.PointCloud.MinZ, v.style.PointCloud.MaxZ,
				v.style.PointCloud.MaxRange)
		}
	}

	for name, layer := range snapshot.Scans {
		if layer == nil {
			continue
		}
		topic := v.topics[name]
		tint := rgbaColor(firstNonEmpty(topic.Color, v.style.LaserScan.Color),
			color.RGBA{0, 255, 136, 255})
		size := v.style.LaserScan.Size
		if topic.PointSize > 0 {
			size = topic.PointSize
		}
		if size <= 0 {
			size = 2
		}
		past := v.scanTrail(name, layer, topic.History)
		for i, sweep := range past {
			fade := float64(i+1) / float64(len(past))
			drawPoints(img, sweep, ppm, v.cx, v.cy, w, h, dimColor(tint, fade), size)
		}
	}

	for name, layer := range snapshot.Paths {
		if layer == nil {
			continue
		}
		topic := v.topics[name]
		tint := rgbaColor(firstNonEmpty(topic.Color, v.style.Path.Color),
			color.RGBA{255, 68, 68, 255})
		width := v.style.Path.Width
		if topic.Width > 0 {
			width = topic.Width
		}
		if width <= 0 {
			width = 2
		}
		drawPolyline(img, layer.Poses, ppm, v.cx, v.cy, w, h, tint, width)
	}

	drawMarkers(img, snapshot.Markers, ppm, v.cx, v.cy, w, h)

	for name, layer := range snapshot.Poses {
		if layer == nil {
			continue
		}
		topic := v.topics[name]
		tint := rgbaColor(firstNonEmpty(topic.Color, v.style.Pose.Color),
			color.RGBA{255, 204, 0, 255})
		size := v.style.Pose.Size
		if topic.PointSize > 0 {
			size = topic.PointSize
		}
		if size <= 0 {
			size = 14
		}
		if v.style.Pose.Covariance {
			ellipse := ellipsePoints(layer.X, layer.Y, layer.CovXX, layer.CovXY, layer.CovYY, 24)
			drawPolyline(img, ellipse, ppm, v.cx, v.cy, w, h, tint, 1)
		}
		drawPose(img, layer.X, layer.Y, layer.Yaw, ppm, v.cx, v.cy, w, h, tint, size)
	}
	for _, layer := range snapshot.Odometry {
		if layer == nil {
			continue
		}
		poseTint := rgbaColor(v.style.Pose.Color, color.RGBA{255, 204, 0, 255})
		poseSize := v.style.Pose.Size
		if poseSize <= 0 {
			poseSize = 14
		}
		drawPose(img, layer.X, layer.Y, layer.Yaw, ppm, v.cx, v.cy, w, h, poseTint, poseSize)
	}
}

// firstNonEmpty picks the topic's own colour when it has one and the type's
// style otherwise.
func firstNonEmpty(values ...string) string {
	for _, value := range values {
		if value != "" {
			return value
		}
	}
	return ""
}

// scanTrail returns the sweeps to draw for a scan, oldest first. A history of
// one is just the newest sweep, which is what the layer itself holds.
func (v *viewport) scanTrail(name string, layer *ScanLayer, history int) [][]float64 {
	if history <= 1 {
		return [][]float64{layer.Points}
	}
	entry := v.trails[name]
	if entry == nil {
		entry = &trail{}
		v.trails[name] = entry
	}
	if entry.stamp != layer.StampNS {
		entry.stamp = layer.StampNS
		entry.scans = append(entry.scans, layer.Points)
		if len(entry.scans) > history {
			entry.scans = entry.scans[len(entry.scans)-history:]
		}
	}
	if len(entry.scans) == 0 {
		return [][]float64{layer.Points}
	}
	return entry.scans
}

// cloudTrail is scanTrail for a point cloud.
func (v *viewport) cloudTrail(name string, layer *PointCloudLayer, history int) []*PointCloudLayer {
	if history <= 1 {
		return []*PointCloudLayer{layer}
	}
	entry := v.trails[name]
	if entry == nil {
		entry = &trail{}
		v.trails[name] = entry
	}
	if entry.stamp != layer.StampNS {
		entry.stamp = layer.StampNS
		entry.clouds = append(entry.clouds, layer)
		if len(entry.clouds) > history {
			entry.clouds = entry.clouds[len(entry.clouds)-history:]
		}
	}
	if len(entry.clouds) == 0 {
		return []*PointCloudLayer{layer}
	}
	return entry.clouds
}

// dimColor scales a colour towards black. The history is drawn with the same
// colour as the newest sweep, only darker, so a trail reads as one thing.
func dimColor(c color.RGBA, factor float64) color.RGBA {
	if factor > 1 {
		factor = 1
	}
	if factor < 0.05 {
		factor = 0.05
	}
	return color.RGBA{
		R: uint8(float64(c.R) * factor),
		G: uint8(float64(c.G) * factor),
		B: uint8(float64(c.B) * factor),
		A: c.A,
	}
}

// rgbaColor parses a "#rrggbb" string into an opaque colour.
func rgbaColor(text string, fallback color.RGBA) color.RGBA {
	if len(text) == 7 && text[0] == '#' {
		var r, g, b uint8
		if _, err := fmt.Sscanf(text, "#%02x%02x%02x", &r, &g, &b); err == nil {
			return color.RGBA{R: r, G: g, B: b, A: 255}
		}
	}
	return fallback
}

// setScene hands the viewport the live scene and repaints.
func (v *viewport) setScene(scene *Scene) {
	v.scene = scene
	v.render()
	v.img.Refresh()
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

// applyView takes the view settings from a freshly loaded config. The pan/zoom
// state is reset by the caller (Fit) rather than preserved: a loaded file is a
// new view, not a continuation of the old one.
func (v *viewport) applyView(cfg *Config) {
	v.style = cfg.Display
	v.topics = cfg.Topics
	if v.scene != nil {
		// The colours may have changed; drop the cached textures so they are
		// built again with the new palette.
		snapshot := v.scene.Snapshot()
		for _, layer := range snapshot.Maps {
			if layer != nil {
				layer.Texture = nil
			}
		}
		for _, layer := range snapshot.Costmaps {
			if layer != nil {
				layer.Texture = nil
			}
		}
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
	v.scale = clamp(v.scale, v.minScale, v.maxScale)
	v.render()
	v.img.Refresh()
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
	if v.toolDrag != nil && v.toolDrag.active {
		v.toolDrag.endX, v.toolDrag.endY = v.worldAt(e.Position)
		v.render()
		v.img.Refresh()
		return
	}
	if !v.showPan {
		return
	}
	v.cx -= float64(e.Dragged.DX) / v.scale
	v.cy += float64(e.Dragged.DY) / v.scale
	v.render()
	v.img.Refresh()
}

func (v *viewport) DragEnd() {}

// setPicked highlights a world point - the Select tool's answer.
func (v *viewport) setPicked(x, y float64) {
	v.picked = &[2]float64{x, y}
	v.render()
	v.img.Refresh()
}

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

// setView restores a remembered look: centre and zoom. A zero scale means
// "nothing remembered" and Fit applies instead.
func (v *viewport) setView(cx, cy, scale float64) {
	if scale <= 0 {
		v.Fit()
		return
	}
	v.cx, v.cy = cx, cy
	v.scale = clamp(scale, v.minScale, v.maxScale)
	v.render()
	v.img.Refresh()
}

// Center is the world point the viewport is looking at.
func (v *viewport) Center() (float64, float64) {
	return v.cx, v.cy
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

// backgroundIsLight decides whether the grid needs dark or light lines.
func backgroundIsLight(c color.Color) bool {
	r, g, b, _ := c.RGBA()
	lum := (299*float64(r) + 587*float64(g) + 114*float64(b)) / 1000
	return lum > 0x8000
}

// drawVLineW and drawHLineW draw a line a few pixels wide; the axes are
// thicker than the grid so they read as axes and not as another cell edge.
func drawVLineW(img *image.RGBA, x int, c color.Color, width int) {
	for i := 0; i < width; i++ {
		drawVLine(img, x+i, c)
	}
}

func drawHLineW(img *image.RGBA, y int, c color.Color, width int) {
	for i := 0; i < width; i++ {
		drawHLine(img, y+i, c)
	}
}

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
