package main

import (
	"image"
	"image/color"
	"testing"
)

// The pose arrow must have a pointed head ahead of the centre, not only a
// shaft: the head is the part that says which way the robot faces.
func TestPoseArrowHasAHead(t *testing.T) {
	img := image.NewRGBA(image.Rect(0, 0, 600, 300))
	tint := color.RGBA{255, 0, 0, 255}
	drawPose(img, 0, 0, 0, 40, 0, 0, 600, 300, tint, 14)

	count := func(fromX, toX int) int {
		total := 0
		for y := 0; y < 300; y++ {
			for x := fromX; x < toX && x < 600; x++ {
				if img.RGBAAt(x, y) == tint {
					total++
				}
			}
		}
		return total
	}
	// body = 14 * 4.8 = 67 px; the tail is the pose point (x=300), the head
	// spans x = centre+0.70*body .. +1.60*body.
	head := count(345, 410)
	shaft := count(300, 345)
	if head < 200 {
		t.Fatalf("head is missing: %d tinted pixels ahead of the waist", head)
	}
	if shaft < 50 {
		t.Fatalf("shaft is missing: %d tinted pixels", shaft)
	}
}
