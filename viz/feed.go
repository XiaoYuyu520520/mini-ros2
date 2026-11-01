// The connection to viz_feed.exe.
//
// The child is started with stdin/stdout pipes. Every frame is a 4 byte
// little-endian length, one kind byte, and the payload; stdout carries only
// frames and stderr carries logs. A request carries an `id`; the reply carries
// the same id, so one reader goroutine can demultiplex replies and asynchronous
// `frame`/`frames`/`error` events.
package main

import (
	"bufio"
	"context"
	"encoding/binary"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"sync"
	"sync/atomic"
	"time"
)

const (
	frameJSON   byte = 1
	frameBinary byte = 2
	maxFrame         = 64 << 20
)

func writeFrame(w io.Writer, kind byte, payload []byte) error {
	if len(payload) > maxFrame {
		return fmt.Errorf("frame too large: %d bytes", len(payload))
	}
	header := make([]byte, 5)
	binary.LittleEndian.PutUint32(header, uint32(len(payload)))
	header[4] = kind
	if _, err := w.Write(header); err != nil {
		return err
	}
	_, err := w.Write(payload)
	return err
}

func readFrame(r io.Reader) (byte, []byte, error) {
	header := make([]byte, 5)
	if _, err := io.ReadFull(r, header); err != nil {
		return 0, nil, err
	}
	length := binary.LittleEndian.Uint32(header)
	if length == 0 || length > maxFrame {
		return 0, nil, fmt.Errorf("bad frame length %d", length)
	}
	payload := make([]byte, length)
	if _, err := io.ReadFull(r, payload); err != nil {
		return 0, nil, err
	}
	return header[4], payload, nil
}

// Event is one asynchronous message from the sidecar. Data is left raw so the
// scene can decode a map without going through map[string]any first.
type Event struct {
	Name       string
	Topic      string
	Kind       string
	Data       json.RawMessage
	Frames     []string
	Message    string
	FixedFrame string
}

type callResult struct {
	data json.RawMessage
	err  error
}

type Feed struct {
	cmd    *exec.Cmd
	stdin  io.WriteCloser
	stdout io.ReadCloser
	stderr io.ReadCloser

	writeMu sync.Mutex
	nextID  atomic.Uint64

	pendingMu sync.Mutex
	pending   map[uint64]chan callResult

	// Events is closed when the child exits. A slow GUI drops the oldest
	// frame instead of blocking the reader, because only the last map/scan is
	// ever drawn.
	Events chan Event

	logMu    sync.Mutex
	logLines []string

	done      chan struct{}
	closeOnce sync.Once

	hello chan Hello
	Hello Hello
}

type Hello struct {
	Protocol   int
	Version    string
	Namespace  string
	FixedFrame string
}

// StartFeed resolves the sidecar, starts it, and waits for its hello. The
// caller must Close it.
func StartFeed(cfg *Config) (*Feed, error) {
	path, err := sidecarPath(cfg)
	if err != nil {
		return nil, err
	}
	cmd := exec.Command(path)
	if cfg.Transport.Namespace != "" {
		cmd.Env = append(os.Environ(), "NAV2_SHM_NAMESPACE="+cfg.Transport.Namespace)
	}
	hideWindow(cmd)

	stdin, err := cmd.StdinPipe()
	if err != nil {
		return nil, err
	}
	stdout, err := cmd.StdoutPipe()
	if err != nil {
		return nil, err
	}
	stderr, err := cmd.StderrPipe()
	if err != nil {
		return nil, err
	}
	if err := cmd.Start(); err != nil {
		return nil, fmt.Errorf("start %s: %w", path, err)
	}

	f := &Feed{
		cmd:     cmd,
		stdin:   stdin,
		stdout:  stdout,
		stderr:  stderr,
		pending: make(map[uint64]chan callResult),
		Events:  make(chan Event, 256),
		done:    make(chan struct{}),
		hello:   make(chan Hello, 1),
	}
	go f.readLoop()
	go f.logLoop()

	select {
	case hello := <-f.hello:
		f.Hello = hello
		return f, nil
	case <-f.done:
		return nil, fmt.Errorf("viz_feed exited before hello: %s", f.LastLog())
	case <-time.After(5 * time.Second):
		f.Close()
		return nil, errors.New("viz_feed did not send hello")
	}
}

func (f *Feed) readLoop() {
	defer close(f.Events)
	defer close(f.done)
	reader := bufio.NewReaderSize(f.stdout, 1<<20)
	for {
		kind, payload, err := readFrame(reader)
		if err != nil {
			f.failPending(err)
			return
		}
		if kind != frameJSON {
			continue
		}
		var probe struct {
			ID     uint64          `json:"id"`
			OK     *bool           `json:"ok"`
			Error  string          `json:"error"`
			Event  string          `json:"event"`
			Result json.RawMessage `json:"result"`
		}
		if err := json.Unmarshal(payload, &probe); err != nil {
			continue
		}
		if probe.Event != "" {
			event := parseEvent(payload)
			if event.Name == "hello" {
				var hello struct {
					Protocol   int    `json:"protocol"`
					Version    string `json:"version"`
					Namespace  string `json:"namespace"`
					FixedFrame string `json:"fixed_frame"`
				}
				_ = json.Unmarshal(payload, &hello)
				value := Hello{
					Protocol: hello.Protocol, Version: hello.Version,
					Namespace: hello.Namespace, FixedFrame: hello.FixedFrame,
				}
				select {
				case f.hello <- value:
				default:
				}
				continue
			}
			select {
			case f.Events <- event:
			default:
				// Drop the oldest so the newest map/scan can get in.
				select {
				case <-f.Events:
				default:
				}
				select {
				case f.Events <- event:
				default:
				}
			}
			continue
		}
		if probe.ID != 0 {
			f.pendingMu.Lock()
			ch := f.pending[probe.ID]
			delete(f.pending, probe.ID)
			f.pendingMu.Unlock()
			if ch == nil {
				continue
			}
			if probe.OK != nil && !*probe.OK {
				ch <- callResult{err: errors.New(probe.Error)}
			} else {
				ch <- callResult{data: probe.Result}
			}
		}
	}
}

func parseEvent(payload []byte) Event {
	var raw struct {
		Event      string          `json:"event"`
		Topic      string          `json:"topic"`
		Kind       string          `json:"kind"`
		Data       json.RawMessage `json:"data"`
		Frames     []string        `json:"frames"`
		Message    string          `json:"message"`
		FixedFrame string          `json:"fixed_frame"`
	}
	_ = json.Unmarshal(payload, &raw)
	return Event{
		Name: raw.Event, Topic: raw.Topic, Kind: raw.Kind, Data: raw.Data,
		Frames: raw.Frames, Message: raw.Message, FixedFrame: raw.FixedFrame,
	}
}

func (f *Feed) logLoop() {
	scanner := bufio.NewScanner(f.stderr)
	scanner.Buffer(make([]byte, 4096), 1<<20)
	for scanner.Scan() {
		line := scanner.Text()
		f.logMu.Lock()
		f.logLines = append(f.logLines, line)
		if len(f.logLines) > 200 {
			f.logLines = f.logLines[len(f.logLines)-200:]
		}
		f.logMu.Unlock()
	}
}

func (f *Feed) LastLog() string {
	f.logMu.Lock()
	defer f.logMu.Unlock()
	if len(f.logLines) == 0 {
		return ""
	}
	return f.logLines[len(f.logLines)-1]
}

func (f *Feed) failPending(err error) {
	f.pendingMu.Lock()
	pending := f.pending
	f.pending = make(map[uint64]chan callResult)
	f.pendingMu.Unlock()
	for _, ch := range pending {
		ch <- callResult{err: err}
	}
}

func (f *Feed) Call(ctx context.Context, op string, args map[string]any) (json.RawMessage, error) {
	select {
	case <-f.done:
		return nil, errors.New("viz_feed is not running")
	default:
	}
	id := f.nextID.Add(1)
	request := make(map[string]any, len(args)+2)
	for key, value := range args {
		request[key] = value
	}
	request["id"] = id
	request["op"] = op
	body, err := json.Marshal(request)
	if err != nil {
		return nil, err
	}
	reply := make(chan callResult, 1)
	f.pendingMu.Lock()
	f.pending[id] = reply
	f.pendingMu.Unlock()

	f.writeMu.Lock()
	err = writeFrame(f.stdin, frameJSON, body)
	f.writeMu.Unlock()
	if err != nil {
		f.pendingMu.Lock()
		delete(f.pending, id)
		f.pendingMu.Unlock()
		return nil, err
	}

	select {
	case result := <-reply:
		return result.data, result.err
	case <-ctx.Done():
		f.pendingMu.Lock()
		delete(f.pending, id)
		f.pendingMu.Unlock()
		return nil, ctx.Err()
	case <-f.done:
		return nil, errors.New("viz_feed exited")
	}
}

func (f *Feed) Subscribe(ctx context.Context, topic, typeName string, transient bool, maxHz float64) error {
	args := map[string]any{"topic": topic, "transient_local": transient}
	if typeName != "" {
		args["type"] = typeName
	}
	if maxHz > 0 {
		args["max_hz"] = maxHz
	}
	_, err := f.Call(ctx, "subscribe", args)
	return err
}

func (f *Feed) Unsubscribe(ctx context.Context, topic string) error {
	_, err := f.Call(ctx, "unsubscribe", map[string]any{"topic": topic})
	return err
}

func (f *Feed) SetFixedFrame(ctx context.Context, frame string) error {
	_, err := f.Call(ctx, "set_fixed_frame", map[string]any{"frame": frame})
	return err
}

func (f *Feed) PublishPose(
	ctx context.Context, topic, typeName, frame string, x, y, yaw float64, times int,
) error {
	_, err := f.Call(ctx, "publish_pose", map[string]any{
		"topic": topic, "type": typeName, "frame": frame,
		"x": x, "y": y, "yaw": yaw, "times": times,
	})
	return err
}

func (f *Feed) Topics(ctx context.Context) (json.RawMessage, error) {
	return f.Call(ctx, "topics", nil)
}

// PublishPoint is rviz's Publish Point tool: one PointStamped on the topic the
// tool is configured with.
func (f *Feed) PublishPoint(
	ctx context.Context, topic, typeName, frame string, x, y, z float64,
) error {
	_, err := f.Call(ctx, "publish_point", map[string]any{
		"topic": topic, "type": typeName, "frame": frame,
		"x": x, "y": y, "z": z,
	})
	return err
}

// Frame is one topic the sidecar knows about.
type Frame struct {
	Name        string   `json:"name"`
	Type        string   `json:"type"`
	Publishers  int      `json:"publishers"`
	Subscribers int      `json:"subscribers"`
	Sources     []string `json:"sources"`
	Remote      bool     `json:"remote"`
}

// TopicList asks the sidecar for the graph.
func (f *Feed) TopicList(ctx context.Context) ([]Frame, error) {
	raw, err := f.Call(ctx, "topics", nil)
	if err != nil {
		return nil, err
	}
	var frames []Frame
	if err := json.Unmarshal(raw, &frames); err != nil {
		return nil, err
	}
	return frames, nil
}

func (f *Feed) Close() {
	f.closeOnce.Do(func() {
		ctx, cancel := context.WithTimeout(context.Background(), 500*time.Millisecond)
		_, _ = f.Call(ctx, "shutdown", nil)
		cancel()
		_ = f.stdin.Close()
		waited := make(chan struct{})
		go func() {
			_ = f.cmd.Wait()
			close(waited)
		}()
		select {
		case <-waited:
		case <-time.After(2 * time.Second):
			_ = f.cmd.Process.Kill()
			<-waited
		}
	})
}

// sidecarPath finds the child. `cmd` wins; otherwise the executable directory
// (next to viz.exe in a package), the repository build directory (go run .),
// and PATH are tried in that order.
func sidecarPath(cfg *Config) (string, error) {
	name := "viz_feed"
	if filepath.Ext(os.Args[0]) == ".exe" || isWindows() {
		name = "viz_feed.exe"
	}
	var candidates []string
	if cfg.Transport.Cmd != "" {
		candidates = append(candidates, cfg.Transport.Cmd)
	}
	if exe, err := os.Executable(); err == nil {
		dir := filepath.Dir(exe)
		candidates = append(candidates,
			filepath.Join(dir, name),
			filepath.Join(dir, "..", "bridge", "windows", "build", name),
			filepath.Join(dir, "..", "..", "bridge", "windows", "build", name),
		)
	}
	if wd, err := os.Getwd(); err == nil {
		candidates = append(candidates,
			filepath.Join(wd, name),
			filepath.Join(wd, "..", "bridge", "windows", "build", name),
		)
	}
	for _, candidate := range candidates {
		if info, err := os.Stat(candidate); err == nil && !info.IsDir() {
			return candidate, nil
		}
	}
	if found, err := exec.LookPath(name); err == nil {
		return found, nil
	}
	return "", fmt.Errorf("viz_feed not found (looked for %s); set transport.cmd in config.json", name)
}
