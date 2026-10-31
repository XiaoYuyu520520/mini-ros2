package main

import (
	"encoding/base64"
	"encoding/json"
	"image"
	"image/color"
	"math"
	"testing"

	"fyne.io/fyne/v2"
)

func TestSceneAppliesFrames(t *testing.T) {
	scene := NewScene()

	cells := []byte{0, 100, 255, 0}
	encoded := base64.StdEncoding.EncodeToString(cells)
	mapEvent := Event{
		Name: "frame", Topic: "/map", Kind: "occupancy_grid",
		Data: json.RawMessage(`{
			"frame": "map", "source_frame": "map", "stamp_ns": 12,
			"width": 2, "height": 2, "resolution": 0.5,
			"origin": [5.0, 6.0, 0.5],
			"data": "` + encoded + `"
		}`),
	}
	scene.Apply(mapEvent)

	layer := scene.Snapshot().Maps["/map"]
	if layer == nil {
		t.Fatal("map not stored")
	}
	if layer.Width != 2 || layer.Height != 2 || layer.Resolution != 0.5 {
		t.Fatalf("grid metadata wrong: %+v", layer)
	}
	if layer.OriginX != 5 || layer.OriginY != 6 || layer.OriginYaw != 0.5 {
		t.Fatalf("origin wrong: %+v", layer)
	}
	if len(layer.Cells) != 4 || layer.Cells[2] != -1 {
		t.Fatalf("cells wrong: %v", layer.Cells)
	}

	scanEvent := Event{
		Name: "frame", Topic: "/scan", Kind: "laser_scan",
		Data: json.RawMessage(`{"frame":"map","stamp_ns":1,"points":[1,2,3,4]}`),
	}
	scene.Apply(scanEvent)
	scan := scene.Snapshot().Scans["/scan"]
	if scan == nil || len(scan.Points) != 4 || scan.Points[2] != 3 {
		t.Fatalf("scan wrong: %+v", scan)
	}

	poseEvent := Event{
		Name: "frame", Topic: "/amcl_pose", Kind: "pose_covariance",
		Data: json.RawMessage(`{"frame":"map","stamp_ns":2,"x":1.5,"y":-2.5,"yaw":0.25,
			"cov":[0.25,0,0.25],"yaw_var":0.068}`),
	}
	scene.Apply(poseEvent)
	pose := scene.Snapshot().Poses["/amcl_pose"]
	if pose == nil || pose.X != 1.5 || pose.Y != -2.5 || pose.Yaw != 0.25 {
		t.Fatalf("pose wrong: %+v", pose)
	}
	if pose.CovXX != 0.25 || pose.CovYY != 0.25 || pose.YawVar != 0.068 {
		t.Fatalf("covariance wrong: %+v", pose)
	}
}

func TestSceneIgnoresUnknownKind(t *testing.T) {
	scene := NewScene()
	scene.Apply(Event{Name: "frame", Topic: "/x", Kind: "point_cloud",
		Data: json.RawMessage(`{"whatever":1}`)})
	if len(scene.Snapshot().Scans) != 0 {
		t.Fatal("unknown kind was stored")
	}
}

// The ellipse is the position covariance in the fixed frame; a pose with a
// 0.5 m standard deviation in x and y has to come out as an ellipse about one
// metre across, not a point.
func TestEllipseFollowsCovariance(t *testing.T) {
	points := ellipsePoints(0, 0, 0.25, 0, 0.25, 32)
	if len(points) < 32 {
		t.Fatalf("ellipse too short: %d", len(points))
	}
	maxRadius := 0.0
	for i := 0; i+1 < len(points); i += 2 {
		radius := math.Hypot(points[i], points[i+1])
		if radius > maxRadius {
			maxRadius = radius
		}
	}
	if math.Abs(maxRadius-0.5) > 0.02 {
		t.Fatalf("radius %v, want 0.5", maxRadius)
	}
}

// A map drawn into a bitmap has to land where the origin says: the cell at
// (0,0) of the grid is at the origin in the fixed frame, and the grid's own y
// axis points up.
func TestDrawMapPlacesOrigin(t *testing.T) {
	// Two by two: cell (0,0) occupied, the rest free.
	layer := &MapLayer{Width: 2, Height: 2, Resolution: 1, Cells: []int8{100, 0, 0, 0}}
	buildTexture(layer,
		color.RGBA{255, 255, 255, 255},
		color.RGBA{0, 0, 0, 255},
		color.RGBA{128, 128, 128, 255},
		1)

	// A 20x20 pixel bitmap, 10 pixels per metre. The grid's lower-left corner
	// is at world (-1,-1), so the whole 2x2 m map is visible.
	const size = 20
	const ppm = 10.0
	img := newTestImage(size, size)
	drawMap(img, layer, ppm, 0, 0, size, size, -1, -1, 0)

	black := countColor(img, color.RGBA{0, 0, 0, 255})
	if black == 0 {
		t.Fatal("the occupied cell was not drawn")
	}
	// Cell (0,0) is the map's lower-left corner, which is the bitmap's
	// lower-left quadrant: x in [0,10), y in (10,20]. The texture is stored
	// top row first, so cell (0,0) is the texture's bottom-left entry.
	minX, minY, maxX, maxY := size, size, -1, -1
	for y := 0; y < size; y++ {
		for x := 0; x < size; x++ {
			if colorAt(img, x, y) == (color.RGBA{0, 0, 0, 255}) {
				minX = min(minX, x)
				minY = min(minY, y)
				maxX = max(maxX, x)
				maxY = max(maxY, y)
			}
		}
	}
	if minX >= size/2 || maxY <= size/2 {
		t.Fatalf("the occupied cell is at x %d..%d, y %d..%d, want the lower left",
			minX, maxX, minY, maxY)
	}
}

func TestDrawPolylineFollowsWorld(t *testing.T) {
	const size = 40
	const ppm = 10.0
	img := newTestImage(size, size)
	// A horizontal line from (-1,0) to (1,0): world y up is screen y down, so
	// it lands on the middle row.
	drawPolyline(img, []float64{-1, 0, 1, 0}, ppm, 0, 0, size, size,
		color.RGBA{255, 0, 0, 255}, 1)
	for y := 0; y < size; y++ {
		for x := 0; x < size; x++ {
			if colorAt(img, x, y) == (color.RGBA{255, 0, 0, 255}) {
				if math.Abs(float64(y)-float64(size)/2) > 1.5 {
					t.Fatalf("the line is on row %d, want the middle", y)
				}
				return
			}
		}
	}
	t.Fatal("nothing was drawn")
}

// --- helpers -----------------------------------------------------------------

func newTestImage(w, h int) *image.RGBA {
	return image.NewRGBA(image.Rect(0, 0, w, h))
}

func colorAt(img *image.RGBA, x, y int) color.RGBA {
	return img.RGBAAt(x, y)
}

func countColor(img *image.RGBA, want color.RGBA) int {
	count := 0
	bounds := img.Bounds()
	for y := bounds.Min.Y; y < bounds.Max.Y; y++ {
		for x := bounds.Min.X; x < bounds.Max.X; x++ {
			if img.RGBAAt(x, y) == want {
				count++
			}
		}
	}
	return count
}

// A per-topic colour has to beat the type's style. The scan is drawn into the
// viewport bitmap, so the test looks at the pixels rather than at a field.
func TestTopicColourOverridesTypeStyle(t *testing.T) {
	cfg := defaultConfig()
	cfg.Topics["/scan"] = TopicConfig{Type: "sensor_msgs/msg/LaserScan", Color: "#ff0000"}
	cfg.Display.LaserScan.Color = "#00ff88"
	v := newViewport(&cfg)
	v.Resize(fyne.NewSize(400, 300))

	scene := NewScene()
	v.setScene(scene)
	scene.Apply(Event{
		Name: "frame", Topic: "/scan", Kind: "laser_scan",
		Data: json.RawMessage(`{"frame":"map","points":[0,0]}`),
	})
	v.render()

	img, ok := v.img.Image.(*image.RGBA)
	if !ok {
		t.Fatalf("viewport image is %T", v.img.Image)
	}
	if countColor(img, color.RGBA{255, 0, 0, 255}) == 0 {
		t.Fatal("the topic colour was not used")
	}
}

// Disabling a topic has to drop its last frame, not merely stop new ones.
func TestSceneRemoveDropsLayers(t *testing.T) {
	scene := NewScene()
	scene.Apply(Event{Name: "frame", Topic: "/scan", Kind: "laser_scan",
		Data: json.RawMessage(`{"frame":"map","points":[1,2]}`)})
	scene.Remove("/scan")
	if len(scene.Snapshot().Scans) != 0 {
		t.Fatal("the scan survived Remove")
	}
}

// A marker topic can carry many shapes and replace them one at a time, so the
// scene keys them by namespace/id. DELETE and DELETEALL are the two actions
// that matter: without them a deleted arrow would stay on screen forever.
func TestSceneAppliesAndDeletesMarkers(t *testing.T) {
	scene := NewScene()
	marker := `{"frame":"map","source_frame":"map","stamp_ns":3,
		"ns":"goal","id":7,"type":0,"action":0,
		"x":1,"y":2,"yaw":0.5,"scale":[2,0.2,0.4],
		"color":[1,0,0,1],"points":[]}`
	scene.Apply(Event{Name: "frame", Topic: "/markers", Kind: "marker",
		Data: json.RawMessage(marker)})

	byID := scene.Snapshot().Markers["/markers"]
	if byID == nil {
		t.Fatal("marker topic not stored")
	}
	layer := byID["goal/7"]
	if layer == nil {
		t.Fatal("marker goal/7 missing")
	}
	if layer.Type != MarkerArrow || layer.X != 1 || layer.Y != 2 || layer.Yaw != 0.5 {
		t.Fatalf("marker fields wrong: %+v", layer)
	}
	if layer.ScaleX != 2 || layer.ScaleY != 0.2 || layer.ScaleZ != 0.4 {
		t.Fatalf("marker scale wrong: %+v", layer)
	}
	if layer.R != 255 || layer.G != 0 || layer.B != 0 || layer.A != 255 {
		t.Fatalf("marker colour wrong: %+v", layer)
	}

	// A second marker on the same topic is a separate shape.
	second := `{"frame":"map","stamp_ns":4,"ns":"goal","id":8,"type":4,
		"x":0,"y":0,"scale":[0,1,0],"color":[0,1,0,1],
		"points":[-1,0,1,0]}`
	scene.Apply(Event{Name: "frame", Topic: "/markers", Kind: "marker",
		Data: json.RawMessage(second)})
	if len(scene.Snapshot().Markers["/markers"]) != 2 {
		t.Fatal("second marker replaced the first")
	}

	// DELETE removes only that id.
	scene.Apply(Event{Name: "frame", Topic: "/markers", Kind: "marker",
		Data: json.RawMessage(`{"ns":"goal","id":7,"action":2}`)})
	byID = scene.Snapshot().Markers["/markers"]
	if _, ok := byID["goal/7"]; ok {
		t.Fatal("deleted marker still present")
	}
	if _, ok := byID["goal/8"]; !ok {
		t.Fatal("DELETE removed the wrong marker")
	}

	// DELETEALL clears the topic.
	scene.Apply(Event{Name: "frame", Topic: "/markers", Kind: "marker",
		Data: json.RawMessage(`{"action":3}`)})
	if _, ok := scene.Snapshot().Markers["/markers"]; ok {
		t.Fatal("DELETEALL left the topic behind")
	}
}

// An arrow at yaw 0 points +x: its pixels must be to the right of the pose,
// not left of it.
func TestDrawArrowPointsAlongYaw(t *testing.T) {
	const size = 60
	const ppm = 10.0
	img := newTestImage(size, size)
	marker := &MarkerLayer{
		Type: MarkerArrow, X: 0, Y: 0, Yaw: 0,
		ScaleX: 2, ScaleY: 0.2, ScaleZ: 0.4,
		R: 255, A: 255,
	}
	drawMarkers(img, map[string]map[string]*MarkerLayer{
		"/markers": {"goal/7": marker},
	}, ppm, 0, 0, size, size)

	minX, maxX := size, -1
	for y := 0; y < size; y++ {
		for x := 0; x < size; x++ {
			if colorAt(img, x, y) == (color.RGBA{255, 0, 0, 255}) {
				minX = min(minX, x)
				maxX = max(maxX, x)
			}
		}
	}
	if maxX < size*3/4 {
		t.Fatalf("arrow did not reach the right side: x %d..%d", minX, maxX)
	}
	// The pose is the arrow's tail: the shaft may start a hair left of it
	// because the line has a width, but it must not reach the far left.
	if minX < size/2-4 {
		t.Fatalf("arrow starts left of the pose: x %d..%d", minX, maxX)
	}
}
