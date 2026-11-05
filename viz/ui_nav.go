// The navigation panel: the GUI's half of the navigation loop.
//
// Until now the "2D Goal Pose" tool published a PoseStamped on `/goal_pose`
// and stopped there. nav2's `bt_navigator` does not act on that topic by
// itself, so the click did not make the robot move - the robot was moved by
// `ros2 action send_goal` somewhere else, and the GUI had no idea how that
// turned out.
//
// The panel is the other half of the loop: the goal goes out as a real
// NavigateToPose action (the same shared-memory action protocol
// `ros2 action send_goal` speaks), and the acceptance, the feedback and the
// result come back as events. The panel shows where the robot is going, what
// the last feedback said, how much distance is left, and offers the one
// button that stops all of it.
//
// The distance and the estimated time are read out of the feedback text the
// sidecar sends. The feedback is a human readable dump of the action's
// feedback message, so the numbers are picked out of it by name; a field that
// is not there is simply not shown, which is better than showing a zero that
// looks like a measurement.

package main

import (
	"fmt"
	"regexp"
	"strconv"
	"strings"
	"sync"
	"time"

	"context"
	"encoding/json"

	"fyne.io/fyne/v2"
	"fyne.io/fyne/v2/container"
	"fyne.io/fyne/v2/widget"
)

// navState is where the goal stands. The names are the ones the sidecar
// reports: `accepted`, `rejected`, `succeeded`, `aborted`, `canceled`,
// `error`, `stopped`.
type navState struct {
	mu        sync.Mutex
	state     string
	goalID    uint64
	action    string
	feedback  string
	distance  float64
	hasDist   bool
	timeLeft  float64
	hasTime   bool
	lastError string
}

func (s *navState) set(state, feedback string) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.state = state
	if feedback != "" {
		s.feedback = feedback
	}
}

func (s *navState) setError(message string) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.state = "error"
	s.lastError = message
}

func (s *navState) snapshot() (state string, feedback string, distance float64,
	hasDistance bool, timeLeft float64, hasTime bool,
) {
	s.mu.Lock()
	defer s.mu.Unlock()
	return s.state, s.feedback, s.distance, s.hasDist, s.timeLeft, s.hasTime
}

// navPanel is the widget tree of the panel. It is a struct so that the event
// handler can update the labels from another thread.
type navPanel struct {
	shell *shell
	state *navState

	targetEntry *widget.Entry
	target      *widget.Label
	status      *widget.Label
	distance    *widget.Label
	feedback    *widget.Label
	cancel      *widget.Button

	lastX     float64
	lastY     float64
	hasTarget bool
}

// newNavPanel builds the panel. The goal target is shown as soon as the user
// clicks one, before the sidecar has answered anything, because the click is
// the moment the user wants to see "where did I just send it".
func newNavPanel(sh *shell) *navPanel {
	p := &navPanel{shell: sh, state: &navState{}}

	p.target = widget.NewLabel(tr("target: -", "目标位置: -"))
	p.status = widget.NewLabel(tr("state: idle", "执行状态: 空闲"))
	p.distance = widget.NewLabel(tr("distance: -", "剩余距离: -"))
	p.feedback = widget.NewLabelWithStyle("", fyne.TextAlignLeading, fyne.TextStyle{Monospace: true})
	p.feedback.Wrapping = fyne.TextWrapWord
	p.cancel = widget.NewButton(tr("Cancel goal", "取消目标"), func() { p.cancelGoal() })
	p.cancel.Disable()

	// The tool buttons are on the toolbar, but the panel is where the
	// outcome is, so it also carries a field for a goal typed by hand. The
	// click on the map is the usual way; a coordinate typed from the status
	// output is sometimes what the user has.
	p.targetEntry = widget.NewEntry()
	p.targetEntry.SetPlaceHolder(tr("x y [yaw]", "x y [偏航角]"))
	p.targetEntry.OnSubmitted = func(text string) { p.sendTypedGoal(text) }

	return p
}

// container is the widget tree the accordion holds.
func (p *navPanel) container() fyne.CanvasObject {
	sendButton := widget.NewButton(tr("Send goal", "发送目标"), func() {
		p.sendTypedGoal(p.targetEntry.Text)
	})
	return container.NewVBox(
		p.target,
		p.status,
		p.distance,
		widget.NewSeparator(),
		container.NewBorder(nil, nil, nil, sendButton, p.targetEntry),
		p.cancel,
		widget.NewSeparator(),
		widget.NewLabel(tr("feedback:", "反馈:")),
		p.feedback,
	)
}

// sendTypedGoal parses the "x y [yaw]" line and sends the goal.
func (p *navPanel) sendTypedGoal(text string) {
	fields := strings.Fields(text)
	if len(fields) < 2 {
		p.state.setError(tr("the target needs at least x and y", "目标至少需要 x 和 y"))
		p.refresh()
		return
	}
	x, err := strconv.ParseFloat(fields[0], 64)
	if err != nil {
		p.state.setError(fmt.Sprintf("x: %v", err))
		p.refresh()
		return
	}
	y, err := strconv.ParseFloat(fields[1], 64)
	if err != nil {
		p.state.setError(fmt.Sprintf("y: %v", err))
		p.refresh()
		return
	}
	yaw := 0.0
	if len(fields) >= 3 {
		parsed, err := strconv.ParseFloat(fields[2], 64)
		if err != nil {
			p.state.setError(fmt.Sprintf("yaw: %v", err))
			p.refresh()
			return
		}
		yaw = parsed
	}
	p.sendGoal(x, y, yaw)
}

// sendGoal asks the sidecar for a NavigateToPose goal at that position.
func (p *navPanel) sendGoal(x, y, yaw float64) {
	feed := p.shell.feed
	if feed == nil {
		p.state.setError(tr("the sidecar is not running", "数据边车未运行"))
		p.refresh()
		return
	}
	action, typeName, frame := p.shell.goalAction()
	p.lastX, p.lastY, p.hasTarget = x, y, true
	p.state.mu.Lock()
	p.state.state = "sending"
	p.state.goalID = 0
	p.state.action = action
	p.state.feedback = ""
	p.state.mu.Unlock()
	p.refresh()

	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()
	if err := feed.SendGoal(ctx, action, typeName, frame, x, y, yaw); err != nil {
		p.state.setError(err.Error())
		p.refresh()
		return
	}
	p.state.mu.Lock()
	p.state.state = "sent"
	p.state.mu.Unlock()
	p.refresh()
}

// cancelGoal stops the running goal.
func (p *navPanel) cancelGoal() {
	feed := p.shell.feed
	if feed == nil {
		return
	}
	ctx, cancel := context.WithTimeout(context.Background(), 6*time.Second)
	defer cancel()
	if err := feed.CancelGoal(ctx); err != nil {
		p.state.setError(err.Error())
		p.refresh()
	}
}

// onEvent folds one action event into the panel. The sidecar sends
// `goal_status`, `goal_feedback` and `goal_result`.
func (p *navPanel) onEvent(event Event) {
	switch event.Name {
	case "goal_status":
		var payload struct {
			State   string `json:"state"`
			Message string `json:"message"`
		}
		_ = json.Unmarshal(event.Data, &payload)
		if payload.Message != "" {
			p.state.setError(payload.Message)
		} else {
			p.state.set(payload.State, "")
		}
	case "goal_feedback":
		var payload struct {
			Text string `json:"text"`
		}
		_ = json.Unmarshal(event.Data, &payload)
		distance, hasDistance := feedbackDistance(payload.Text)
		timeLeft, hasTime := feedbackTime(payload.Text)
		p.state.mu.Lock()
		p.state.feedback = payload.Text
		p.state.distance = distance
		p.state.hasDist = hasDistance
		p.state.timeLeft = timeLeft
		p.state.hasTime = hasTime
		if p.state.state == "sent" || p.state.state == "sending" || p.state.state == "accepted" {
			p.state.state = "running"
		}
		p.state.mu.Unlock()
	case "goal_result":
		var payload struct {
			State string `json:"state"`
			Text  string `json:"text"`
		}
		_ = json.Unmarshal(event.Data, &payload)
		p.state.mu.Lock()
		p.state.state = payload.State
		if payload.Text != "" {
			p.state.feedback = payload.Text
		}
		p.state.mu.Unlock()
	}
	p.refresh()
}

// refresh redraws the panel. It is called from the event thread, so every
// widget is touched through `fyne.Do`.
func (p *navPanel) refresh() {
	state, feedback, distance, hasDistance, timeLeft, hasTime := p.state.snapshot()
	target := p.targetText()
	fyne.Do(func() {
		p.target.SetText(target)
		p.status.SetText(tr("state: ", "执行状态: ") + stateText(state))
		p.distance.SetText(distanceText(distance, hasDistance, timeLeft, hasTime))
		p.feedback.SetText(feedback)
		if state == "running" || state == "sent" || state == "sending" || state == "accepted" {
			p.cancel.Enable()
		} else {
			p.cancel.Disable()
		}
	})
}

// targetText is the "where did I send it" line. It is filled in by the
// click handler, not by the sidecar.
func (p *navPanel) targetText() string {
	if !p.hasTarget {
		return tr("target: -", "目标位置: -")
	}
	return fmt.Sprintf(tr("target: x %.2f m  y %.2f m", "目标位置: x %.2f m  y %.2f m"),
		p.lastX, p.lastY)
}

// stateText turns the sidecar's state into something a person reads.
func stateText(state string) string {
	switch state {
	case "":
		return tr("idle", "空闲")
	case "sending":
		return tr("sending", "正在发送")
	case "sent":
		return tr("waiting for the server", "等待动作服务器")
	case "accepted":
		return tr("accepted", "目标已接受")
	case "running":
		return tr("running", "执行中")
	case "succeeded":
		return tr("succeeded", "成功")
	case "aborted":
		return tr("aborted", "中止")
	case "canceled":
		return tr("canceled", "已取消")
	case "rejected":
		return tr("rejected", "被拒绝")
	case "error":
		return tr("error", "错误")
	case "stopped":
		return tr("stopped", "已停止")
	default:
		return state
	}
}

// distanceText is the "how far is left" line.
func distanceText(distance float64, hasDistance bool, timeLeft float64, hasTime bool) string {
	parts := []string{}
	if hasDistance {
		parts = append(parts, fmt.Sprintf(tr("distance: %.2f m", "剩余距离: %.2f m"), distance))
	}
	if hasTime {
		parts = append(parts, fmt.Sprintf(tr("time: %.1f s", "预计剩余: %.1f 秒"), timeLeft))
	}
	if len(parts) == 0 {
		return tr("distance: -", "剩余距离: -")
	}
	return strings.Join(parts, "   ")
}

// The feedback is a human readable dump of the action's feedback message,
// e.g.
//
//   current_pose:
//     header: ...
//     pose: ...
//   navigation_time: 0.000000
//   estimated_time_remaining: 0.000000
//   number_of_recoveries: 0
//   distance_remaining: 1.420000
//
// The two numbers a panel shows are picked out of it by name. A field that is
// absent is not shown at all, which is better than a zero that looks like a
// measurement.

var feedbackDistancePattern = regexp.MustCompile(`distance_remaining:\s*([0-9.eE+-]+)`)
var feedbackTimePattern = regexp.MustCompile(`estimated_time_remaining:\s*([0-9.eE+-]+)`)

func feedbackDistance(text string) (float64, bool) {
	return firstNumber(feedbackDistancePattern, text)
}

func feedbackTime(text string) (float64, bool) {
	return firstNumber(feedbackTimePattern, text)
}

func firstNumber(pattern *regexp.Regexp, text string) (float64, bool) {
	match := pattern.FindStringSubmatch(text)
	if len(match) < 2 {
		return 0, false
	}
	value, err := strconv.ParseFloat(match[1], 64)
	if err != nil {
		return 0, false
	}
	return value, true
}
