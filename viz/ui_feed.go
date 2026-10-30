// The feed controller: start viz_feed, subscribe to the configured topics,
// fold events into the scene, and keep the status bar honest.
//
// It is deliberately a separate object from the widget tree. The sidecar can
// die and be restarted without rebuilding the window; the renderer only ever
// sees the Scene.
package main

import (
	"context"
	"fmt"
	"sort"
	"sync"
	"sync/atomic"
	"time"

	"fyne.io/fyne/v2"
	"fyne.io/fyne/v2/widget"
)

type feedController struct {
	view   *viewport
	scene  *Scene
	getCfg func() Config

	sidecarLabel *widget.Label
	frameLabel   *widget.Label

	mu         sync.Mutex
	feed       *Feed
	subscribed map[string]bool
	rates      map[string]int

	// onConnected runs after the child answers hello, so the topic picker can
	// be filled without the user pressing Refresh first.
	onConnected func()

	closing atomic.Bool
	done    chan struct{}
}

func newFeedController(
	view *viewport, scene *Scene, getCfg func() Config,
	sidecarLabel, frameLabel *widget.Label,
) *feedController {
	return &feedController{
		view: view, scene: scene, getCfg: getCfg,
		sidecarLabel: sidecarLabel, frameLabel: frameLabel,
		subscribed: map[string]bool{}, rates: map[string]int{},
		done: make(chan struct{}),
	}
}

func (c *feedController) start() {
	c.setStatus("sidecar: starting")
	go c.loop()
}

func (c *feedController) close() {
	if c.closing.Swap(true) {
		return
	}
	close(c.done)
	c.mu.Lock()
	feed := c.feed
	c.feed = nil
	c.mu.Unlock()
	if feed != nil {
		feed.Close()
	}
}

// applyFixedFrame switches the fixed frame on the running child, or just
// records it while the child is down. The next connection uses the config.
func (c *feedController) applyFixedFrame(cfg Config) {
	frame := fixedFrameOf(&cfg)
	c.scene.SetFixedFrame(frame)
	c.setFrame(frame)
	c.mu.Lock()
	feed := c.feed
	c.mu.Unlock()
	if feed == nil {
		return
	}
	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	if err := feed.SetFixedFrame(ctx, frame); err != nil {
		c.setStatus("sidecar: fixed frame: " + err.Error())
	}
	cancel()
}

// setEnabled turns one display off or on. Off means "stop delivering and drop
// the last frame", not "hide": a disabled map must not keep drawing stale
// walls under the next scan. The sidecar keeps the subscription but stops
// forwarding, so turning it back on is a re-subscribe that immediately gets
// the latched sample again.
func (c *feedController) setEnabled(topic string, on bool) {
	c.mu.Lock()
	feed := c.feed
	c.mu.Unlock()
	if feed == nil {
		return
	}
	if on {
		c.resubscribe()
		return
	}
	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	err := feed.Unsubscribe(ctx, topic)
	cancel()
	if err != nil {
		c.setStatus("sidecar: unsubscribe " + topic + ": " + err.Error())
	}
	c.scene.Remove(topic)
	fyne.Do(func() { c.view.Refresh() })
}

// resubscribe is called after a config load: the fixed frame and the topic
// list may have changed. Old sidecar subscriptions stay alive (the sidecar
// keeps them for a quick re-enable), but every name is offered again.
func (c *feedController) resubscribe() {
	c.mu.Lock()
	c.subscribed = map[string]bool{}
	c.mu.Unlock()
}

func (c *feedController) loop() {
	for !c.closing.Load() {
		cfg := c.getCfg()
		feed, err := StartFeed(&cfg)
		if err != nil {
			c.setStatus("sidecar: " + err.Error())
			if !c.sleep(3 * time.Second) {
				return
			}
			continue
		}

		c.mu.Lock()
		c.feed = feed
		c.subscribed = map[string]bool{}
		c.mu.Unlock()

		frame := fixedFrameOf(&cfg)
		c.scene.SetFixedFrame(frame)
		c.setFrame(frame)
		ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
		if err := feed.SetFixedFrame(ctx, frame); err != nil {
			c.setStatus("sidecar: fixed frame: " + err.Error())
		}
		cancel()
		if c.onConnected != nil {
			fyne.Do(c.onConnected)
		}

		c.consume(feed)
		feed.Close()
		c.mu.Lock()
		if c.feed == feed {
			c.feed = nil
		}
		c.mu.Unlock()
		if c.closing.Load() {
			return
		}
		c.setStatus("sidecar: exited, restarting")
		if !c.sleep(2 * time.Second) {
			return
		}
	}
}

func (c *feedController) sleep(d time.Duration) bool {
	select {
	case <-c.done:
		return false
	case <-time.After(d):
		return true
	}
}

func (c *feedController) consume(feed *Feed) {
	render := time.NewTicker(33 * time.Millisecond)
	status := time.NewTicker(time.Second)
	retry := time.NewTicker(2 * time.Second)
	defer render.Stop()
	defer status.Stop()
	defer retry.Stop()

	dirty := false
	c.subscribePending(feed)
	for {
		select {
		case <-c.done:
			return
		case event, ok := <-feed.Events:
			if !ok {
				return
			}
			switch event.Name {
			case "frame":
				c.scene.Apply(event)
				c.mu.Lock()
				c.subscribed[event.Topic] = true
				c.rates[event.Topic]++
				c.mu.Unlock()
				dirty = true
			case "frames":
				c.scene.SetFrames(event.Frames)
			case "error":
				c.scene.SetError(event.Topic, event.Message)
				c.setStatus(fmt.Sprintf("sidecar: %s: %s", event.Topic, event.Message))
			}
		case <-retry.C:
			c.subscribePending(feed)
		case <-render.C:
			if dirty {
				dirty = false
				fyne.Do(func() { c.view.Refresh() })
			}
		case <-status.C:
			c.publishRates()
		}
	}
}

// subscribePending offers every configured topic that has not been confirmed
// yet. A topic whose publisher has not started has no type in the graph; the
// sidecar answers with an error and this ticker tries again, which is what
// makes "start the viewer first" work.
func (c *feedController) subscribePending(feed *Feed) {
	cfg := c.getCfg()
	names := sortedTopicNames(&cfg)
	for _, name := range names {
		if c.closing.Load() {
			return
		}
		if name == "/tf" || name == "/tf_static" {
			continue
		}
		topic, ok := cfg.Topics[name]
		if !ok || !topic.enabled() {
			continue
		}
		c.mu.Lock()
		done := c.subscribed[name]
		c.mu.Unlock()
		if done {
			continue
		}
		ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
		err := feed.Subscribe(ctx, name, topic.Type, transientFor(topic), 0)
		cancel()
		if err != nil {
			c.setStatus(fmt.Sprintf("sidecar: %s: %v (retrying)", name, err))
			continue
		}
		c.mu.Lock()
		c.subscribed[name] = true
		c.mu.Unlock()
	}
}

func (c *feedController) publishRates() {
	c.mu.Lock()
	rates := make(map[string]int, len(c.rates))
	for topic, count := range c.rates {
		rates[topic] = count
		c.rates[topic] = 0
	}
	active := len(c.subscribed)
	c.mu.Unlock()

	type row struct {
		topic string
		count int
	}
	rows := make([]row, 0, len(rates))
	for topic, count := range rates {
		if count > 0 {
			rows = append(rows, row{topic, count})
		}
	}
	sort.Slice(rows, func(i, j int) bool { return rows[i].count > rows[j].count })
	text := fmt.Sprintf("sidecar: %d topic(s)", active)
	for i, entry := range rows {
		if i == 3 {
			break
		}
		text += fmt.Sprintf("  %s %dHz", entry.topic, entry.count)
	}
	if len(rows) == 0 {
		text += "  waiting for data"
	}
	c.setStatus(text)
}

// graphTopics asks the sidecar for the shim graph. It is used by the topic
// picker; a caller that runs before the child is up gets an error.
func (c *feedController) graphTopics() ([]Frame, error) {
	c.mu.Lock()
	feed := c.feed
	c.mu.Unlock()
	if feed == nil {
		return nil, fmt.Errorf("sidecar is not running")
	}
	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()
	return feed.TopicList(ctx)
}

// publishTool sends a goal or an initial pose through the running sidecar.
func (c *feedController) publishTool(
	topic, typeName, frame string, x, y, yaw float64,
) error {
	c.mu.Lock()
	feed := c.feed
	c.mu.Unlock()
	if feed == nil {
		return fmt.Errorf("sidecar is not running")
	}
	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()
	return feed.PublishPose(ctx, topic, typeName, frame, x, y, yaw, 3)
}

func (c *feedController) setStatus(text string) {
	fyne.Do(func() {
		if c.sidecarLabel == nil {
			return
		}
		c.sidecarLabel.SetText(text)
		c.sidecarLabel.Importance = widget.LowImportance
		c.sidecarLabel.Refresh()
	})
}

func (c *feedController) setFrame(frame string) {
	fyne.Do(func() {
		if c.frameLabel != nil {
			c.frameLabel.SetText("frame: " + frame)
			c.frameLabel.Refresh()
		}
	})
}

func fixedFrameOf(cfg *Config) string {
	if view, ok := cfg.Views["view"]; ok && view.FixedFrame != "" {
		return view.FixedFrame
	}
	return "map"
}

// transientFor is the GUI's half of the QoS decision. The sidecar's shared
// memory has no QoS, but a transient_local subscription is what makes it read
// the already latched sample instead of waiting for the next publication.
func transientFor(topic TopicConfig) bool {
	switch topic.QOS.Durability {
	case "transient_local":
		return true
	case "volatile":
		return false
	}
	switch topic.Type {
	case "nav_msgs/msg/OccupancyGrid", "tf2_msgs/msg/TFMessage":
		return true
	}
	return false
}
