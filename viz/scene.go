// The scene: what the sidecar said, kept in the form the viewport draws.
//
// The sidecar already did the work that is easy to get wrong - decoding the
// message and transforming it into the view's fixed frame - so the GUI only
// ever holds geometry in world coordinates. Nothing here knows about ROS
// message layouts; the payloads arrive as JSON with the shapes the protocol
// documents.
package main

import (
	"encoding/base64"
	"encoding/json"
	"image/color"
	"math"
	"sync"
)

// MapLayer is an occupancy grid. The cells stay as they came (0 free, 100
// occupied, -1 unknown) and the texture is built from them.
type MapLayer struct {
	Width      int
	Height     int
	Resolution float64
	// Origin is the pose of the grid's lower-left cell in the fixed frame.
	OriginX   float64
	OriginY   float64
	OriginYaw float64
	Cells     []int8
	// Texture is the RGBA bitmap built from Cells, in grid order.
	Texture  []byte
	TextureW int
	TextureH int
	StampNS  int64
	Frame    string
}

// ScanLayer is a point cloud of one laser sweep, already in the fixed frame.
type ScanLayer struct {
	Points  []float64 // x0,y0,x1,y1,...
	StampNS int64
	Frame   string
}

// PathLayer is a plan: x,y,yaw per pose.
type PathLayer struct {
	Poses   []float64
	StampNS int64
	Frame   string
}

// PoseLayer is a robot pose with its position covariance.
type PoseLayer struct {
	X       float64
	Y       float64
	Yaw     float64
	CovXX   float64
	CovXY   float64
	CovYY   float64
	YawVar  float64
	StampNS int64
	Frame   string
}

// OdometryLayer is a pose with a velocity.
type OdometryLayer struct {
	PoseLayer
	VX         float64
	WZ         float64
	ChildFrame string
}

// Scene holds the latest value of every display. It is written by the feed
// reader and read by the render loop, so it has a lock; the render loop takes
// a snapshot copy, never the live slices.
type Scene struct {
	mu sync.Mutex

	Maps     map[string]*MapLayer
	Scans    map[string]*ScanLayer
	Paths    map[string]*PathLayer
	Poses    map[string]*PoseLayer
	Odometry map[string]*OdometryLayer

	// Errors from the sidecar, keyed by topic.
	Errors map[string]string
	// Frames is the TF frame list.
	Frames []string
	// FixedFrame is what the sidecar transforms into.
	FixedFrame string
}

func NewScene() *Scene {
	return &Scene{
		Maps:     map[string]*MapLayer{},
		Scans:    map[string]*ScanLayer{},
		Paths:    map[string]*PathLayer{},
		Poses:    map[string]*PoseLayer{},
		Odometry: map[string]*OdometryLayer{},
		Errors:   map[string]string{},
	}
}

// Apply folds one frame event into the scene. Unknown kinds are ignored: the
// sidecar may grow a new one and an old GUI should not fall over.
func (s *Scene) Apply(event Event) {
	s.mu.Lock()
	defer s.mu.Unlock()

	switch event.Kind {
	case "occupancy_grid":
		var payload struct {
			Frame       string    `json:"frame"`
			SourceFrame string    `json:"source_frame"`
			StampNS     int64     `json:"stamp_ns"`
			Width       int       `json:"width"`
			Height      int       `json:"height"`
			Resolution  float64   `json:"resolution"`
			Origin      []float64 `json:"origin"`
			Data        string    `json:"data"`
		}
		if err := json.Unmarshal(event.Data, &payload); err != nil {
			return
		}
		raw, err := base64.StdEncoding.DecodeString(payload.Data)
		if err != nil {
			return
		}
		cells := make([]int8, len(raw))
		for i, b := range raw {
			cells[i] = int8(b)
		}
		layer := &MapLayer{
			Width: payload.Width, Height: payload.Height, Resolution: payload.Resolution,
			Cells: cells, StampNS: payload.StampNS, Frame: payload.Frame,
		}
		if len(payload.Origin) >= 3 {
			layer.OriginX, layer.OriginY, layer.OriginYaw =
				payload.Origin[0], payload.Origin[1], payload.Origin[2]
		}
		s.Maps[event.Topic] = layer
	case "laser_scan":
		var payload struct {
			Frame   string    `json:"frame"`
			StampNS int64     `json:"stamp_ns"`
			Points  []float64 `json:"points"`
		}
		if err := json.Unmarshal(event.Data, &payload); err != nil {
			return
		}
		s.Scans[event.Topic] = &ScanLayer{
			Points: payload.Points, StampNS: payload.StampNS, Frame: payload.Frame}
	case "path":
		var payload struct {
			Frame   string    `json:"frame"`
			StampNS int64     `json:"stamp_ns"`
			Poses   []float64 `json:"poses"`
		}
		if err := json.Unmarshal(event.Data, &payload); err != nil {
			return
		}
		s.Paths[event.Topic] = &PathLayer{
			Poses: payload.Poses, StampNS: payload.StampNS, Frame: payload.Frame}
	case "pose":
		var payload struct {
			Frame   string  `json:"frame"`
			StampNS int64   `json:"stamp_ns"`
			X       float64 `json:"x"`
			Y       float64 `json:"y"`
			Yaw     float64 `json:"yaw"`
		}
		if err := json.Unmarshal(event.Data, &payload); err != nil {
			return
		}
		s.Poses[event.Topic] = &PoseLayer{
			X: payload.X, Y: payload.Y, Yaw: payload.Yaw,
			StampNS: payload.StampNS, Frame: payload.Frame}
	case "pose_covariance":
		var payload struct {
			Frame   string    `json:"frame"`
			StampNS int64     `json:"stamp_ns"`
			X       float64   `json:"x"`
			Y       float64   `json:"y"`
			Yaw     float64   `json:"yaw"`
			Cov     []float64 `json:"cov"`
			YawVar  float64   `json:"yaw_var"`
		}
		if err := json.Unmarshal(event.Data, &payload); err != nil {
			return
		}
		layer := &PoseLayer{
			X: payload.X, Y: payload.Y, Yaw: payload.Yaw,
			StampNS: payload.StampNS, Frame: payload.Frame}
		if len(payload.Cov) >= 3 {
			layer.CovXX, layer.CovXY, layer.CovYY =
				payload.Cov[0], payload.Cov[1], payload.Cov[2]
		}
		layer.YawVar = payload.YawVar
		s.Poses[event.Topic] = layer
	case "odometry":
		var payload struct {
			Frame      string    `json:"frame"`
			ChildFrame string    `json:"child_frame"`
			StampNS    int64     `json:"stamp_ns"`
			X          float64   `json:"x"`
			Y          float64   `json:"y"`
			Yaw        float64   `json:"yaw"`
			VX         float64   `json:"vx"`
			WZ         float64   `json:"wz"`
			Cov        []float64 `json:"cov"`
		}
		if err := json.Unmarshal(event.Data, &payload); err != nil {
			return
		}
		layer := &OdometryLayer{
			PoseLayer: PoseLayer{X: payload.X, Y: payload.Y, Yaw: payload.Yaw,
				StampNS: payload.StampNS, Frame: payload.Frame},
			VX: payload.VX, WZ: payload.WZ, ChildFrame: payload.ChildFrame}
		if len(payload.Cov) >= 3 {
			layer.CovXX, layer.CovXY, layer.CovYY =
				payload.Cov[0], payload.Cov[1], payload.Cov[2]
		}
		s.Odometry[event.Topic] = layer
	default:
		return
	}
	delete(s.Errors, event.Topic)
}

// Remove drops everything a topic contributed. Disabling a display should not
// leave its last map or scan on screen until the next message.
func (s *Scene) Remove(topic string) {
	s.mu.Lock()
	defer s.mu.Unlock()
	delete(s.Maps, topic)
	delete(s.Scans, topic)
	delete(s.Paths, topic)
	delete(s.Poses, topic)
	delete(s.Odometry, topic)
	delete(s.Errors, topic)
}

// SetError records a sidecar error for a topic (a missing transform, most of
// the time).
func (s *Scene) SetError(topic, message string) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.Errors[topic] = message
}

// SetFrames records the TF frame list.
func (s *Scene) SetFrames(frames []string) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.Frames = append([]string(nil), frames...)
}

// SetFixedFrame records the frame the sidecar transforms into.
func (s *Scene) SetFixedFrame(frame string) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.FixedFrame = frame
}

// Snapshot is a copy of the scene for the render loop. The slices inside are
// shared, not copied: the sidecar replaces a layer wholesale rather than
// editing it, so an in-flight frame may be a hair old but never torn.
type Snapshot struct {
	Maps       map[string]*MapLayer
	Scans      map[string]*ScanLayer
	Paths      map[string]*PathLayer
	Poses      map[string]*PoseLayer
	Odometry   map[string]*OdometryLayer
	Errors     map[string]string
	Frames     []string
	FixedFrame string
}

func (s *Scene) Snapshot() Snapshot {
	s.mu.Lock()
	defer s.mu.Unlock()
	copyErrors := make(map[string]string, len(s.Errors))
	for key, value := range s.Errors {
		copyErrors[key] = value
	}
	maps := make(map[string]*MapLayer, len(s.Maps))
	for key, value := range s.Maps {
		maps[key] = value
	}
	scans := make(map[string]*ScanLayer, len(s.Scans))
	for key, value := range s.Scans {
		scans[key] = value
	}
	paths := make(map[string]*PathLayer, len(s.Paths))
	for key, value := range s.Paths {
		paths[key] = value
	}
	poses := make(map[string]*PoseLayer, len(s.Poses))
	for key, value := range s.Poses {
		poses[key] = value
	}
	odometry := make(map[string]*OdometryLayer, len(s.Odometry))
	for key, value := range s.Odometry {
		odometry[key] = value
	}
	return Snapshot{
		Maps: maps, Scans: scans, Paths: paths, Poses: poses,
		Odometry: odometry, Errors: copyErrors,
		Frames: append([]string(nil), s.Frames...), FixedFrame: s.FixedFrame,
	}
}

// buildTexture turns the cells into an RGBA bitmap. The palette is the one
// rviz uses: unknown is a mid grey, free is near white, occupied is black and
// the values in between are a gradient, because a costmap writes them.
func buildTexture(layer *MapLayer, free, occupied, unknown color.RGBA, alpha float64) {
	if layer.Width <= 0 || layer.Height <= 0 {
		return
	}
	layer.TextureW = layer.Width
	layer.TextureH = layer.Height
	layer.Texture = make([]byte, layer.Width*layer.Height*4)
	for i := 0; i < layer.Width*layer.Height; i++ {
		value := int8(0)
		if i < len(layer.Cells) {
			value = layer.Cells[i]
		}
		var r, g, b uint8
		switch {
		case value < 0:
			r, g, b = unknown.R, unknown.G, unknown.B
		case value == 0:
			r, g, b = free.R, free.G, free.B
		default:
			// 100 is fully occupied, 0 is free; the values in between are
			// the gradient a costmap writes.
			fraction := float64(value) / 100.0
			r = uint8(float64(occupied.R)*fraction + float64(free.R)*(1-fraction))
			g = uint8(float64(occupied.G)*fraction + float64(free.G)*(1-fraction))
			b = uint8(float64(occupied.B)*fraction + float64(free.B)*(1-fraction))
		}
		layer.Texture[i*4+0] = r
		layer.Texture[i*4+1] = g
		layer.Texture[i*4+2] = b
		layer.Texture[i*4+3] = uint8(alpha * 255)
	}
}

// ellipsePoints returns the outline of the position covariance ellipse. The
// covariance is in the fixed frame already (the sidecar rotated it), so this
// is a plain 2x2 eigen decomposition.
func ellipsePoints(cx, cy, covXX, covXY, covYY float64, steps int) []float64 {
	if covXX <= 0 && covYY <= 0 {
		return nil
	}
	trace := covXX + covYY
	det := covXX*covYY - covXY*covXY
	if det <= 0 {
		return nil
	}
	disc := math.Sqrt(math.Max(0, trace*trace/4-det))
	largest := trace/2 + disc
	smallest := math.Max(0, trace/2-disc)
	if largest <= 0 {
		return nil
	}
	// The eigenvector of the larger eigenvalue.
	var vx, vy float64
	if math.Abs(covXY) > 1e-12 {
		vx = largest - covYY
		vy = covXY
	} else if covXX >= covYY {
		vx, vy = 1, 0
	} else {
		vx, vy = 0, 1
	}
	norm := math.Hypot(vx, vy)
	if norm == 0 {
		return nil
	}
	vx, vy = vx/norm, vy/norm
	// The perpendicular axis.
	px, py := -vy, vx
	a := math.Sqrt(largest)
	b := math.Sqrt(smallest)
	points := make([]float64, 0, steps*2)
	for i := 0; i < steps; i++ {
		angle := 2 * math.Pi * float64(i) / float64(steps)
		ca, sa := math.Cos(angle)*a, math.Sin(angle)*b
		points = append(points, cx+vx*ca+px*sa, cy+vy*ca+py*sa)
	}
	return points
}
