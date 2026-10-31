package main

import (
	"encoding/json"
	"image"
	"image/color"
	"image/png"
	"math"
	"os"
	"path/filepath"
	"testing"
)

// countColour counts the pixels of one colour in an image.
func countColour(img *image.RGBA, want color.RGBA) int {
	total := 0
	bounds := img.Bounds()
	for y := bounds.Min.Y; y < bounds.Max.Y; y++ {
		for x := bounds.Min.X; x < bounds.Max.X; x++ {
			got := img.RGBAAt(x, y)
			if got.R == want.R && got.G == want.G && got.B == want.B && got.A == want.A {
				total++
			}
		}
	}
	return total
}

// nearColour counts the pixels that are close to a colour, for the cells that
// are mixed with something else (an anti-aliased edge, a translucent blit).
func nearColour(img *image.RGBA, want color.RGBA, tolerance int) int {
	total := 0
	bounds := img.Bounds()
	for y := bounds.Min.Y; y < bounds.Max.Y; y++ {
		for x := bounds.Min.X; x < bounds.Max.X; x++ {
			got := img.RGBAAt(x, y)
			if math.Abs(float64(int(got.R)-int(want.R))) <= float64(tolerance) &&
				math.Abs(float64(int(got.G)-int(want.G))) <= float64(tolerance) &&
				math.Abs(float64(int(got.B)-int(want.B))) <= float64(tolerance) {
				total++
			}
		}
	}
	return total
}

// The costmap has to be drawn with its palette and the cloud with its height
// colours: both are drawn by the same code the window uses, so the picture
// this test writes is the picture the window shows.
func TestRenderCostmapAndCloud(t *testing.T) {
	scene := NewScene()
	// A 4x4 costmap with one lethal cell, one inscribed cell and one high
	// cost cell. The origin puts it right of and above the view centre.
	scene.Apply(Event{Name: "frame", Topic: "/costmap", Kind: "costmap",
		Data: json.RawMessage(`{"frame":"map","source_frame":"map","stamp_ns":1,
			"width":4,"height":4,"resolution":0.25,"origin":[0.5,0.5,0],
			"layer":"master","data":[0,0,100,0, 0,254,0,0, 0,0,255,0, 0,0,0,0]}`)})
	scene.Apply(Event{Name: "frame", Topic: "/points", Kind: "point_cloud2",
		Data: json.RawMessage(`{"frame":"map","source_frame":"map","stamp_ns":2,
			"points":[0,0, 0.5,0, 0,0.5, 0.5,0.5],
			"heights":[0,0.5,1,1.5],"intensities":[0,0,0,0]}`)})

	snapshot := scene.Snapshot()
	costmap := snapshot.Costmaps["/costmap"]
	cloud := snapshot.Clouds["/points"]
	if costmap == nil || cloud == nil {
		t.Fatalf("layers missing: costmap=%v cloud=%v", costmap, cloud)
	}

	img := image.NewRGBA(image.Rect(0, 0, 400, 300))
	// The view centre is the middle of the picture and 100 pixels per metre.
	centerX, centerY := 0.0, 0.0
	ppm := 100.0

	buildCostmapTexture(costmap,
		color.RGBA{0x2b, 0x6c, 0xff, 0xff}, color.RGBA{0xff, 0x40, 0x40, 0xff},
		color.RGBA{0x00, 0xe5, 0xff, 0xff}, color.RGBA{0x10, 0x10, 0x10, 0xff},
		color.RGBA{0x7a, 0x7a, 0x7a, 0xff}, 0.75)
	drawCostmap(img, costmap, ppm, centerX, centerY, 400, 300)
	drawCloud(img, cloud, ppm, centerX, centerY, 400, 300,
		color.RGBA{0x00, 0xd0, 0xff, 0xff}, 8, true, 0, 0, 0)

	// The cost 100 cell is the high colour, the 254 cell the inscribed colour
	// and the 255 cell the lethal colour; each of them must be on the screen.
	high := nearColour(img, color.RGBA{0xff, 0x40, 0x40, 0xff}, 20)
	inscribed := nearColour(img, color.RGBA{0x00, 0xe5, 0xff, 0xff}, 20)
	lethal := nearColour(img, color.RGBA{0x10, 0x10, 0x10, 0xff}, 12)
	if high == 0 {
		t.Fatal("the cost 100 cell is not on the picture")
	}
	if inscribed == 0 {
		t.Fatal("the inscribed cell is not on the picture")
	}
	if lethal == 0 {
		t.Fatal("the lethal cell is not on the picture")
	}

	// The cloud is coloured by its height: the lowest point is blue and the
	// highest one red, so both ends of the ramp have to be there.
	blue := nearColour(img, color.RGBA{0, 0, 255, 0xff}, 30)
	red := nearColour(img, color.RGBA{255, 0, 0, 0xff}, 30)
	if blue == 0 {
		t.Fatal("the low point of the cloud is not blue")
	}
	if red == 0 {
		t.Fatal("the high point of the cloud is not red")
	}

	// The picture is written out so a person can look at it.
	out := filepath.Join(t.TempDir(), "costmap_cloud.png")
	file, err := os.Create(out)
	if err != nil {
		t.Fatalf("cannot write the picture: %v", err)
	}
	if err := png.Encode(file, img); err != nil {
		t.Fatalf("cannot encode the picture: %v", err)
	}
	file.Close()
	t.Logf("picture written to %s", out)
}
