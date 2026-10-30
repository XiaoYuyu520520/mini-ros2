package main

import (
	"bytes"
	"context"
	"fmt"
	"os"
	"strings"
	"testing"
	"time"
)

func TestFrameRoundTrip(t *testing.T) {
	var buffer bytes.Buffer
	payload := []byte(`{"op":"hello","id":7}`)
	if err := writeFrame(&buffer, frameJSON, payload); err != nil {
		t.Fatal(err)
	}
	kind, got, err := readFrame(&buffer)
	if err != nil {
		t.Fatal(err)
	}
	if kind != frameJSON {
		t.Fatalf("kind %d, want %d", kind, frameJSON)
	}
	if !bytes.Equal(got, payload) {
		t.Fatalf("payload %q, want %q", got, payload)
	}
}

func TestFrameRejectsAbsurdLength(t *testing.T) {
	// A length that claims more than maxFrame must be refused before the
	// caller allocates it.
	buffer := bytes.NewBuffer([]byte{0xff, 0xff, 0xff, 0x7f, frameJSON})
	if _, _, err := readFrame(buffer); err == nil {
		t.Fatal("oversized frame accepted")
	}
}

// This is the real child. It is skipped where the sidecar has not been built
// (the Go-only CI job), and it uses a namespace of its own so it cannot join a
// running nav2 graph on a developer machine.
func TestSidecarHelloAndTopics(t *testing.T) {
	cfg := defaultConfig()
	cfg.Transport.Namespace = fmt.Sprintf("go_viz_feed_test_%d", os.Getpid())

	feed, err := StartFeed(&cfg)
	if err != nil {
		if strings.Contains(err.Error(), "not found") {
			t.Skip(err)
		}
		t.Fatalf("StartFeed: %v", err)
	}
	defer feed.Close()

	if feed.Hello.Protocol != 1 {
		t.Fatalf("protocol %d, want 1", feed.Hello.Protocol)
	}
	if !strings.Contains(feed.Hello.Namespace, "go_viz_feed_test_") {
		t.Fatalf("namespace %q did not reach the child", feed.Hello.Namespace)
	}

	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	topics, err := feed.Topics(ctx)
	if err != nil {
		t.Fatalf("topics: %v", err)
	}
	if !bytes.Contains(topics, []byte("/goal_pose")) {
		t.Fatalf("tool topic not advertised: %s", topics)
	}
	if !bytes.Contains(topics, []byte("/initialpose")) {
		t.Fatalf("initial pose not advertised: %s", topics)
	}
}
