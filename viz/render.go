// Drawing the scene.
//
// The viewport is one bitmap (see viewport.go): the grid, then the map, then
// the scan, then the paths and the robot. Everything is drawn in device
// pixels, so the same code works at 96 dpi and at 200%.
//
// The map is the only expensive layer: its texture is rebuilt when the map
// message changes, and each frame just blits it with the grid's rotation
// applied. A nearest-neighbour blit is deliberate - a bilinear one would
// blur the wall edges, which are the thing a person looks at.
package main

import (
	"image"
	"image/color"
	"math"
	"sort"
)

// drawMap paints an occupancy grid into the bitmap.
//
// The grid's cells are axis aligned in their own frame; the origin's yaw says
// how that frame sits in the fixed frame. The inverse mapping is used per
// destination pixel, so the destination bounds are the map's corners.
func drawMap(
	target *image.RGBA, layer *MapLayer, ppm float64, centerX, centerY float64,
	width, height int, originX, originY, originYaw float64,
) {
	if layer == nil || layer.Texture == nil || layer.Width <= 0 || layer.Height <= 0 {
		return
	}
	resolution := layer.Resolution
	if resolution <= 0 {
		resolution = 0.05
	}
	mapWidth := float64(layer.Width) * resolution
	mapHeight := float64(layer.Height) * resolution

	// The map's four corners in the fixed frame.
	cos := math.Cos(originYaw)
	sin := math.Sin(originYaw)
	corners := [][2]float64{
		{0, 0},
		{mapWidth, 0},
		{mapWidth, mapHeight},
		{0, mapHeight},
	}
	minX, minY := math.Inf(1), math.Inf(1)
	maxX, maxY := math.Inf(-1), math.Inf(-1)
	for _, corner := range corners {
		x := originX + corner[0]*cos - corner[1]*sin
		y := originY + corner[0]*sin + corner[1]*cos
		minX = math.Min(minX, x)
		maxX = math.Max(maxX, x)
		minY = math.Min(minY, y)
		maxY = math.Max(maxY, y)
	}

	// To screen pixels.
	screenMinX := (minX-centerX)*ppm + float64(width)/2
	screenMaxX := (maxX-centerX)*ppm + float64(width)/2
	// World y grows up, screen y grows down, so the world's maxY is the
	// bitmap's top edge and its minY is the bottom edge.
	screenMinY := (centerY-maxY)*ppm + float64(height)/2
	screenMaxY := (centerY-minY)*ppm + float64(height)/2
	x0 := int(math.Floor(screenMinX))
	x1 := int(math.Ceil(screenMaxX))
	y0 := int(math.Floor(screenMinY))
	y1 := int(math.Ceil(screenMaxY))
	if x0 < 0 {
		x0 = 0
	}
	if y0 < 0 {
		y0 = 0
	}
	if x1 > width {
		x1 = width
	}
	if y1 > height {
		y1 = height
	}
	if x1 <= x0 || y1 <= y0 {
		return
	}

	invPpm := 1 / ppm
	for py := y0; py < y1; py++ {
		// Screen y grows down, world y grows up.
		worldY := centerY - (float64(py)+0.5-float64(height)/2)*invPpm
		for px := x0; px < x1; px++ {
			worldX := centerX + (float64(px)+0.5-float64(width)/2)*invPpm
			dx := worldX - originX
			dy := worldY - originY
			// Into the grid's own frame.
			gx := dx*cos + dy*sin
			gy := -dx*sin + dy*cos
			if gx < 0 || gy < 0 {
				continue
			}
			cellX := int(gx / resolution)
			cellY := int(gy / resolution)
			if cellX >= layer.Width || cellY >= layer.Height {
				continue
			}
			// The texture is stored row by row from the top of the image, and
			// the grid's y axis points up: cell (0,0) is the grid's lower-left
			// and the texture's bottom-left.
			index := (cellY*layer.Width + cellX) * 4
			if index+3 >= len(layer.Texture) {
				continue
			}
			alpha := layer.Texture[index+3]
			if alpha == 0 {
				continue
			}
			target.Set(px, py, color.RGBA{
				R: layer.Texture[index], G: layer.Texture[index+1],
				B: layer.Texture[index+2], A: alpha})
		}
	}
}

// buildCostmapTexture turns the cost values into an RGBA bitmap.
//
// The palette is the one a costmap display uses: free space stays transparent
// so the map below shows through, the costs 1..100 fade from the low colour to
// the high one, and the special values keep their own colours - 254 is an
// inscribed obstacle, 255 a lethal one, 253 unknown.
func buildCostmapTexture(
	layer *CostmapLayer, low, high, inscribed, lethal, unknown color.RGBA, alpha float64,
) {
	if layer.Width <= 0 || layer.Height <= 0 {
		return
	}
	layer.TextureW = layer.Width
	layer.TextureH = layer.Height
	layer.Texture = make([]byte, layer.Width*layer.Height*4)
	for i := 0; i < layer.Width*layer.Height; i++ {
		cost := 0
		if i < len(layer.Cells) {
			cost = layer.Cells[i]
		}
		var r, g, b uint8
		opacity := alpha
		switch {
		case cost == 0:
			// Free: nothing to draw.
			opacity = 0
			r, g, b = 0, 0, 0
		case cost == 253:
			r, g, b = unknown.R, unknown.G, unknown.B
		case cost == 254:
			r, g, b = inscribed.R, inscribed.G, inscribed.B
		case cost == 255:
			r, g, b = lethal.R, lethal.G, lethal.B
		default:
			// 1..100 is the interesting range; anything above it is a
			// scaled version of the same idea.
			fraction := float64(cost) / 100.0
			if fraction > 1 {
				fraction = 1
			}
			r = uint8(float64(low.R)*(1-fraction) + float64(high.R)*fraction)
			g = uint8(float64(low.G)*(1-fraction) + float64(high.G)*fraction)
			b = uint8(float64(low.B)*(1-fraction) + float64(high.B)*fraction)
		}
		layer.Texture[i*4+0] = r
		layer.Texture[i*4+1] = g
		layer.Texture[i*4+2] = b
		layer.Texture[i*4+3] = uint8(opacity * 255)
	}
}

// drawCostmap blits a costmap with the same inverse-mapping blit a map uses;
// only the texture (and therefore the palette) differs.
func drawCostmap(
	target *image.RGBA, layer *CostmapLayer, ppm float64,
	centerX, centerY float64, width, height int,
) {
	if layer == nil || layer.Texture == nil {
		return
	}
	drawMap(target, &MapLayer{
		Width: layer.Width, Height: layer.Height, Resolution: layer.Resolution,
		OriginX: layer.OriginX, OriginY: layer.OriginY, OriginYaw: layer.OriginYaw,
		Texture: layer.Texture,
	}, ppm, centerX, centerY, width, height,
		layer.OriginX, layer.OriginY, layer.OriginYaw)
}

// drawPoints paints a scan: one small filled square per return.
func drawPoints(target *image.RGBA, points []float64, ppm float64, centerX, centerY float64, width, height int, tint color.RGBA, size float64) {
	if len(points) < 2 {
		return
	}
	half := int(math.Max(1, size/2))
	for i := 0; i+1 < len(points); i += 2 {
		x := (points[i]-centerX)*ppm + float64(width)/2
		y := (centerY-points[i+1])*ppm + float64(height)/2
		ix := int(math.Round(x))
		iy := int(math.Round(y))
		for dy := -half; dy <= half; dy++ {
			for dx := -half; dx <= half; dx++ {
				px, py := ix+dx, iy+dy
				if px < 0 || py < 0 || px >= width || py >= height {
					continue
				}
				target.Set(px, py, tint)
			}
		}
	}
}

// drawCloud paints a point cloud. The colour is made of the height when the
// publisher sends one and the style asks for it, so a cloud reads as a
// landscape rather than a blob of dots.
func drawCloud(
	target *image.RGBA, layer *PointCloudLayer, ppm float64,
	centerX, centerY float64, width, height int,
	tint color.RGBA, size float64, byHeight bool, minZ, maxZ float64, maxRange float64,
) {
	if layer == nil || len(layer.Points) < 2 {
		return
	}
	if byHeight {
		if minZ == 0 && maxZ == 0 {
			minZ, maxZ = heightRange(layer.Heights)
		}
	}
	span := maxZ - minZ
	if span <= 0 {
		span = 1
	}
	half := int(math.Max(1, size/2))
	for i := 0; i+1 < len(layer.Points); i += 2 {
		x := layer.Points[i]
		y := layer.Points[i+1]
		if maxRange > 0 {
			if math.Hypot(x-centerX, y-centerY) > maxRange {
				continue
			}
		}
		colour := tint
		if byHeight && i/2 < len(layer.Heights) {
			fraction := (layer.Heights[i/2] - minZ) / span
			if fraction < 0 {
				fraction = 0
			}
			if fraction > 1 {
				fraction = 1
			}
			colour = heightColor(fraction)
			colour.A = tint.A
		}
		sx := int(math.Round((x-centerX)*ppm + float64(width)/2))
		sy := int(math.Round((centerY-y)*ppm + float64(height)/2))
		for dy := -half; dy <= half; dy++ {
			for dx := -half; dx <= half; dx++ {
				px, py := sx+dx, sy+dy
				if px < 0 || py < 0 || px >= width || py >= height {
					continue
				}
				target.Set(px, py, colour)
			}
		}
	}
}

// heightRange is the span of the heights in a cloud, used when the style does
// not name one.
func heightRange(heights []float64) (float64, float64) {
	if len(heights) == 0 {
		return 0, 1
	}
	low, high := heights[0], heights[0]
	for _, value := range heights {
		if value < low {
			low = value
		}
		if value > high {
			high = value
		}
	}
	if high <= low {
		high = low + 1
	}
	return low, high
}

// heightColor is the rainbow a height map is drawn with: blue at the bottom,
// red at the top.
func heightColor(fraction float64) color.RGBA {
	clamp := func(value float64) uint8 {
		if value < 0 {
			return 0
		}
		if value > 1 {
			return 255
		}
		return uint8(value*255 + 0.5)
	}
	return color.RGBA{
		R: clamp(fraction),
		G: clamp(1 - math.Abs(2*fraction-1)),
		B: clamp(1 - fraction),
		A: 255,
	}
}

// drawPolyline paints a path, one segment at a time, with a line width in
// pixels.
func drawPolyline(target *image.RGBA, points []float64, ppm float64, centerX, centerY float64, width, height int, tint color.RGBA, thickness float64) {
	if len(points) < 4 {
		return
	}
	for i := 0; i+3 < len(points); i += 2 {
		x0 := (points[i]-centerX)*ppm + float64(width)/2
		y0 := (centerY-points[i+1])*ppm + float64(height)/2
		x1 := (points[i+2]-centerX)*ppm + float64(width)/2
		y1 := (centerY-points[i+3])*ppm + float64(height)/2
		drawLine(target, x0, y0, x1, y1, tint, thickness, width, height)
	}
}

// drawLine is a plain Bresenham with a square brush.
func drawLine(target *image.RGBA, x0, y0, x1, y1 float64, tint color.RGBA, thickness float64, width, height int) {
	dx := x1 - x0
	dy := y1 - y0
	steps := int(math.Max(math.Abs(dx), math.Abs(dy)))
	if steps == 0 {
		steps = 1
	}
	if steps > 8192 {
		steps = 8192
	}
	half := int(math.Max(0, math.Round(thickness/2)))
	for i := 0; i <= steps; i++ {
		t := float64(i) / float64(steps)
		px := int(math.Round(x0 + dx*t))
		py := int(math.Round(y0 + dy*t))
		for by := -half; by <= half; by++ {
			for bx := -half; bx <= half; bx++ {
				sx, sy := px+bx, py+by
				if sx < 0 || sy < 0 || sx >= width || sy >= height {
					continue
				}
				target.Set(sx, sy, tint)
			}
		}
	}
}

// drawMarkers paints every marker of every marker topic. The sidecar has
// already put the poses and points in the fixed frame, so this is pure 2D
// drawing; the interesting part is the type switch, because one topic can
// carry an arrow, a line strip and a cube list at the same time.
func drawMarkers(
	target *image.RGBA, markers map[string]map[string]*MarkerLayer,
	ppm float64, centerX, centerY float64, width, height int,
) {
	topics := make([]string, 0, len(markers))
	for topic := range markers {
		topics = append(topics, topic)
	}
	sort.Strings(topics)
	for _, topic := range topics {
		byID := markers[topic]
		keys := make([]string, 0, len(byID))
		for key := range byID {
			keys = append(keys, key)
		}
		sort.Strings(keys)
		for _, key := range keys {
			drawMarker(target, byID[key], ppm, centerX, centerY, width, height)
		}
	}
}

func drawMarker(
	target *image.RGBA, marker *MarkerLayer,
	ppm float64, centerX, centerY float64, width, height int,
) {
	if marker == nil {
		return
	}
	tint := color.RGBA{R: marker.R, G: marker.G, B: marker.B, A: marker.A}
	if tint.A == 0 {
		tint = color.RGBA{255, 0, 255, 255}
	}
	switch marker.Type {
	case MarkerArrow:
		drawArrow(
			target, marker.X, marker.Y, marker.Yaw,
			marker.ScaleX, marker.ScaleY, marker.ScaleZ,
			ppm, centerX, centerY, width, height, tint)
	case MarkerArrowStrip:
		// Consecutive pairs are separate arrows: start, end, start, end.
		for i := 0; i+3 < len(marker.Points); i += 4 {
			x0, y0 := marker.Points[i], marker.Points[i+1]
			x1, y1 := marker.Points[i+2], marker.Points[i+3]
			length := math.Hypot(x1-x0, y1-y0)
			if length <= 0 {
				continue
			}
			drawArrow(
				target, x0, y0, math.Atan2(y1-y0, x1-x0),
				length, marker.ScaleY, marker.ScaleZ,
				ppm, centerX, centerY, width, height, tint)
		}
	case MarkerLineStrip:
		drawPolyline(target, marker.Points, ppm, centerX, centerY, width, height,
			tint, markerWidth(marker, ppm))
	case MarkerLineList:
		for i := 0; i+3 < len(marker.Points); i += 4 {
			drawLine(
				target,
				(marker.Points[i]-centerX)*ppm+float64(width)/2,
				(centerY-marker.Points[i+1])*ppm+float64(height)/2,
				(marker.Points[i+2]-centerX)*ppm+float64(width)/2,
				(centerY-marker.Points[i+3])*ppm+float64(height)/2,
				tint, markerWidth(marker, ppm), width, height)
		}
	case MarkerCubeList, MarkerSphereList, MarkerPoints:
		size := math.Max(1, marker.ScaleX*ppm)
		drawPoints(target, marker.Points, ppm, centerX, centerY, width, height, tint, size)
	case MarkerTriangleList:
		for i := 0; i+5 < len(marker.Points); i += 6 {
			toScreen := func(j int) (float64, float64) {
				return (marker.Points[j]-centerX)*ppm + float64(width)/2,
					(centerY-marker.Points[j+1])*ppm + float64(height)/2
			}
			x0, y0 := toScreen(i)
			x1, y1 := toScreen(i + 2)
			x2, y2 := toScreen(i + 4)
			fillTriangle(target, x0, y0, x1, y1, x2, y2, tint, width, height)
		}
	case MarkerTextFacing:
		// There is no text rasteriser in this 2D bitmap; a small cross marks
		// the anchor so a text marker is at least visible.
		drawMarkerPoint(target, marker.X, marker.Y, ppm, centerX, centerY,
			width, height, tint, 3)
	case MarkerCube, MarkerSphere, MarkerCylinder:
		drawMarkerPoint(target, marker.X, marker.Y, ppm, centerX, centerY,
			width, height, tint, math.Max(2, marker.ScaleX*ppm))
	default:
		// MESH_RESOURCE and any future type: show the anchor rather than
		// silently dropping it.
		drawMarkerPoint(target, marker.X, marker.Y, ppm, centerX, centerY,
			width, height, tint, 3)
	}
}

func markerWidth(marker *MarkerLayer, ppm float64) float64 {
	if marker.ScaleY > 0 {
		return math.Max(1, marker.ScaleY*ppm)
	}
	return 1
}

func drawMarkerPoint(
	target *image.RGBA, x, y, ppm, centerX, centerY float64,
	width, height int, tint color.RGBA, size float64,
) {
	drawPoints(target, []float64{x, y}, ppm, centerX, centerY, width, height, tint, size)
}

// drawArrow paints an RViz ARROW: a shaft from the pose to the tip and a
// triangular head. Scale x is the length, y the shaft diameter and z the head
// diameter (all in metres); a zero component falls back to a visible default.
func drawArrow(
	target *image.RGBA, x, y, yaw, length, diameter, head float64,
	ppm float64, centerX, centerY float64, width, height int, tint color.RGBA,
) {
	if length <= 0 {
		length = 1
	}
	if diameter <= 0 {
		diameter = 0.1
	}
	if head <= 0 {
		head = math.Max(diameter*2, length*0.25)
	}
	cos, sin := math.Cos(yaw), math.Sin(yaw)
	// The shaft starts at the pose and ends where the head begins; the head
	// is the last part of the arrow, so the base sits *before* the tip.
	headLength := math.Min(length*0.9, math.Max(head, diameter*2))
	baseWorld := length - headLength
	tailX := (x-centerX)*ppm + float64(width)/2
	tailY := (centerY-y)*ppm + float64(height)/2
	baseX := (x+cos*baseWorld-centerX)*ppm + float64(width)/2
	baseY := (centerY-(y+sin*baseWorld))*ppm + float64(height)/2
	tipX := (x+cos*length-centerX)*ppm + float64(width)/2
	tipY := (centerY-(y+sin*length))*ppm + float64(height)/2

	drawLine(target, tailX, tailY, baseX, baseY, tint,
		math.Max(1, diameter*ppm), width, height)

	half := math.Max(1, head*ppm/2)
	perpX, perpY := -sin, cos
	leftX := baseX + perpX*half
	leftY := baseY - perpY*half
	rightX := baseX - perpX*half
	rightY := baseY + perpY*half
	fillTriangle(target, tipX, tipY, leftX, leftY, rightX, rightY, tint, width, height)
}

// drawPose paints a robot: a triangle for the body and an arrow for the
// heading, plus the covariance ellipse when there is one.
// drawPose paints a robot pose: a filled arrow with a dark outline, so it
// reads on a white map as well as on a dark grid. The outline is drawn first
// and the tint fills a slightly smaller copy of the same polygon.
func drawPose(target *image.RGBA, x, y, yaw float64, ppm float64, centerX, centerY float64, width, height int, tint color.RGBA, size float64) {
	screenX := (x-centerX)*ppm + float64(width)/2
	screenY := (centerY-y)*ppm + float64(height)/2
	// The heading in screen space: world yaw is counter-clockwise, screen y
	// grows down, so the arrow's angle is mirrored.
	angle := -yaw
	cos, sin := math.Cos(angle), math.Sin(angle)
	body := size * math.Max(1, ppm*0.12)
	if body < 8 {
		body = 8
	}
	// A point given as (forward, left) in the arrow's own frame.
	point := func(forward, left float64) [2]float64 {
		return [2]float64{
			screenX + cos*forward - sin*left,
			screenY + sin*forward + cos*left,
		}
	}
	// A notched arrow: tip, two head corners, a waist, and a short tail.
	outer := [][2]float64{
		point(1.00, 0.00),
		point(0.52, 0.52),
		point(0.40, 0.18),
		point(-0.85, 0.18),
		point(-0.85, -0.18),
		point(0.40, -0.18),
		point(0.52, -0.52),
	}
	// The dark rim gives the shape an edge on a light background.
	fillPolygon(target, outer, color.RGBA{30, 30, 30, 255}, width, height)
	inner := make([][2]float64, len(outer))
	for i, vertex := range outer {
		inner[i] = [2]float64{
			screenX + (vertex[0]-screenX)*0.78,
			screenY + (vertex[1]-screenY)*0.78,
		}
	}
	fillPolygon(target, inner, tint, width, height)
	fillCircle(target, screenX, screenY, math.Max(1.5, body*0.10), tint, width, height)
}

// fillPolygon paints a convex or concave polygon with the even-odd rule: one
// span per scan line, which is all a shape this size needs.
func fillPolygon(target *image.RGBA, points [][2]float64, tint color.RGBA, width, height int) {
	if len(points) < 3 {
		return
	}
	minY, maxY := points[0][1], points[0][1]
	for _, point := range points[1:] {
		minY = math.Min(minY, point[1])
		maxY = math.Max(maxY, point[1])
	}
	first := int(math.Floor(minY))
	last := int(math.Ceil(maxY))
	if first < 0 {
		first = 0
	}
	if last >= height {
		last = height - 1
	}
	for y := first; y <= last; y++ {
		scan := float64(y) + 0.5
		var crossings []float64
		for i := 0; i < len(points); i++ {
			a := points[i]
			b := points[(i+1)%len(points)]
			if (a[1] <= scan && b[1] > scan) || (b[1] <= scan && a[1] > scan) {
				t := (scan - a[1]) / (b[1] - a[1])
				crossings = append(crossings, a[0]+t*(b[0]-a[0]))
			}
		}
		sort.Float64s(crossings)
		for i := 0; i+1 < len(crossings); i += 2 {
			from := int(math.Round(crossings[i]))
			to := int(math.Round(crossings[i+1]))
			for x := from; x <= to; x++ {
				if x >= 0 && x < width {
					target.Set(x, y, tint)
				}
			}
		}
	}
}

// fillCircle paints a small filled disc; the pose's centre dot uses it.
func fillCircle(target *image.RGBA, centreX, centreY, radius float64, tint color.RGBA, width, height int) {
	first := int(math.Floor(centreY - radius))
	last := int(math.Ceil(centreY + radius))
	for y := first; y <= last; y++ {
		for x := int(math.Floor(centreX - radius)); x <= int(math.Ceil(centreX+radius)); x++ {
			if x < 0 || y < 0 || x >= width || y >= height {
				continue
			}
			dx := float64(x) + 0.5 - centreX
			dy := float64(y) + 0.5 - centreY
			if dx*dx+dy*dy <= radius*radius {
				target.Set(x, y, tint)
			}
		}
	}
}

func fillTriangle(target *image.RGBA, x0, y0, x1, y1, x2, y2 float64, tint color.RGBA, width, height int) {
	minX := int(math.Floor(math.Min(x0, math.Min(x1, x2))))
	maxX := int(math.Ceil(math.Max(x0, math.Max(x1, x2))))
	minY := int(math.Floor(math.Min(y0, math.Min(y1, y2))))
	maxY := int(math.Ceil(math.Max(y0, math.Max(y1, y2))))
	denominator := (y1-y2)*(x0-x2) + (x2-x1)*(y0-y2)
	if denominator == 0 {
		return
	}
	for py := minY; py <= maxY; py++ {
		for px := minX; px <= maxX; px++ {
			x := float64(px) + 0.5
			y := float64(py) + 0.5
			l0 := ((y1-y2)*(x-x2) + (x2-x1)*(y-y2)) / denominator
			l1 := ((y2-y0)*(x-x2) + (x0-x2)*(y-y2)) / denominator
			l2 := 1 - l0 - l1
			if l0 < 0 || l1 < 0 || l2 < 0 {
				continue
			}
			if px < 0 || py < 0 || px >= width || py >= height {
				continue
			}
			target.Set(px, py, tint)
		}
	}
}
