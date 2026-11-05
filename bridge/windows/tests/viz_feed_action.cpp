// bridge_viz_feed_action - the sidecar's navigation action, end to end.
//
// The GUI used to publish a PoseStamped on `/goal_pose` and stop there, which
// does not move the robot: nav2's `bt_navigator` acts on the
// `NavigateToPose` *action*, not on that topic. The sidecar now speaks the
// action protocol directly, the same one `ros2 action send_goal` speaks, and
// the GUI shows the acceptance, the feedback and the result.
//
// This test drives the real `viz_feed` binary over its own stdin/stdout
// protocol - the frames, not the internals - with a stand-in action server on
// the other side of shared memory. So it covers the three things that can
// only be wrong together:
//
//   * the goal really lands on `<name>/_goal` with the id in front of it,
//   * the acceptance, the feedback and the result come back as the events the
//     GUI reads, and
//   * a cancel reaches the server and the goal ends as canceled.
//
// The stand-in is the same one `bridge_action_roundtrip` uses, so the six
// channel names and the payload layouts are the ones the daemon plays back
// too. The one thing this test cannot check is the C++ side of the GUI, and
// that is covered by `viz/ui_nav_test.go` in the Go tree.

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include <signal.h>

#ifdef _WIN32
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#include <process.h>
#else
#include <poll.h>
#include <unistd.h>
#include <sys/wait.h>
#endif

#include "nlohmann/json.hpp"

#include "nav2_msgs/action/navigate_to_pose.hpp"
#include "ros2_bridge/action_registry.hpp"
#include "ros2_bridge/shm_topic.hpp"
#include "ros2_shim/graph.hpp"
#include "ros2_shim/serialization.hpp"

namespace
{

using json = nlohmann::json;
using NavigateToPose = nav2_msgs::action::NavigateToPose;

int g_failures = 0;
int g_checks = 0;

void check(bool ok, const std::string & what)
{
  ++g_checks;
  if (ok) {
    std::printf("  ok   %s\n", what.c_str());
  } else {
    ++g_failures;
    std::printf("  FAIL %s\n", what.c_str());
  }
}

// ------------------------------------------------------------- the stand-in

// A server for `/navigate_to_pose`: it accepts the goal, answers with two
// feedback messages and a result. With `hold_result` it keeps the goal
// running, which is what the cancel case needs.
class FakeNavigator
{
public:
  explicit FakeNavigator(bool hold_result)
  : hold_result_(hold_result)
  {
    std::string error;
    goals_ = ros2_bridge::create_action_channel(kAction, "goal", &error);
    goal_acks_ = ros2_bridge::create_action_channel(kAction, "goal_ack", &error);
    cancels_ = ros2_bridge::create_action_channel(kAction, "cancel", &error);
    cancel_acks_ = ros2_bridge::create_action_channel(kAction, "cancel_ack", &error);
    feedbacks_ = ros2_bridge::create_action_channel(kAction, "feedback", &error);
    results_ = ros2_bridge::create_action_channel(kAction, "result", &error);
    if (!goals_ || !goal_acks_ || !cancels_ || !cancel_acks_ || !feedbacks_ || !results_) {
      std::printf("  FAIL cannot create the channels of %s: %s\n", kAction,
        error.c_str());
      ++g_failures;
      return;
    }
    goals_->seek_to_latest();
    cancels_->seek_to_latest();
    ros2_shim::graph::ProcessRegistry::instance().add_action(
      kAction, "nav2_msgs/action/NavigateToPose", "remote:test");
    running_ = true;
    worker_ = std::thread([this] {loop();});
  }

  ~FakeNavigator()
  {
    running_ = false;
    if (worker_.joinable()) {
      worker_.join();
    }
    ros2_shim::graph::ProcessRegistry::instance().remove_action(kAction, "remote:test");
  }

  int goals() const {return goal_count_.load();}
  int cancels() const {return cancel_count_.load();}
  // The pose the last goal carried, so the test can check the numbers the GUI
  // asked for really arrived.
  double last_x() const {return last_x_.load();}
  double last_y() const {return last_y_.load();}

private:
  void loop()
  {
    std::vector<std::uint8_t> buffer(goals_->slot_size());
    while (running_.load()) {
      std::size_t size = 0;
      if (goals_->subscribe(buffer.data(), buffer.size(), &size, 20) &&
        size > sizeof(std::uint64_t))
      {
        std::uint64_t id = 0;
        std::memcpy(&id, buffer.data(), sizeof(id));
        // The goal arrives in the shim's layout, id in front of it.
        NavigateToPose::Goal goal{};
        if (ros2_shim::deserialize(
              buffer.data() + sizeof(id), size - sizeof(id), goal))
        {
          last_x_.store(goal.pose.pose.position.x);
          last_y_.store(goal.pose.pose.position.y);
        }
        goal_count_++;
        std::vector<std::uint8_t> ack(sizeof(id) + 1);
        std::memcpy(ack.data(), &id, sizeof(id));
        ack[sizeof(id)] = 1;  // accepted
        goal_acks_->publish(ack.data(), ack.size());

        if (!hold_result_) {
          sendFeedback(id, 1.42);
          sendResult(id, ros2_bridge::ShimResultCode::Succeeded);
        }
        continue;
      }

      std::size_t cancel_size = 0;
      if (cancels_->subscribe(buffer.data(), buffer.size(), &cancel_size, 20) &&
        cancel_size >= sizeof(std::uint64_t))
      {
        std::uint64_t id = 0;
        std::memcpy(&id, buffer.data(), sizeof(id));
        cancel_count_++;
        std::vector<std::uint8_t> ack(sizeof(id) + 1);
        std::memcpy(ack.data(), &id, sizeof(id));
        ack[sizeof(id)] = 1;  // the cancel is accepted
        cancel_acks_->publish(ack.data(), ack.size());
        sendResult(id, ros2_bridge::ShimResultCode::Canceled);
      }
    }
  }

  void sendFeedback(std::uint64_t id, double distance)
  {
    NavigateToPose::Feedback feedback{};
    feedback.distance_remaining = static_cast<float>(distance);
    feedback.number_of_recoveries = 0;
    const auto payload = ros2_shim::serialize(feedback);
    std::vector<std::uint8_t> out(sizeof(id) + payload.size());
    std::memcpy(out.data(), &id, sizeof(id));
    std::memcpy(out.data() + sizeof(id), payload.data(), payload.size());
    feedbacks_->publish(out.data(), out.size());
  }

  void sendResult(std::uint64_t id, ros2_bridge::ShimResultCode code)
  {
    NavigateToPose::Result result{};
    result.error_code = 0;
    const auto payload = ros2_shim::serialize(result);
    std::vector<std::uint8_t> out(sizeof(id) + 1 + payload.size());
    std::memcpy(out.data(), &id, sizeof(id));
    out[sizeof(id)] = static_cast<std::uint8_t>(code);
    std::memcpy(out.data() + sizeof(id) + 1, payload.data(), payload.size());
    results_->publish(out.data(), out.size());
  }

  static constexpr const char * kAction = "/navigate_to_pose";

  std::shared_ptr<shm_transport::ShmChannel> goals_;
  std::shared_ptr<shm_transport::ShmChannel> goal_acks_;
  std::shared_ptr<shm_transport::ShmChannel> cancels_;
  std::shared_ptr<shm_transport::ShmChannel> cancel_acks_;
  std::shared_ptr<shm_transport::ShmChannel> feedbacks_;
  std::shared_ptr<shm_transport::ShmChannel> results_;
  std::atomic<bool> running_{false};
  std::atomic<int> goal_count_{0};
  std::atomic<int> cancel_count_{0};
  std::atomic<double> last_x_{0.0};
  std::atomic<double> last_y_{0.0};
  bool hold_result_ = false;
  std::thread worker_;
};

// ------------------------------------------------------------- the child

// The sidecar, as a child process, with its own stdin and stdout.
class Sidecar
{
public:
  explicit Sidecar(const std::string & executable)
  {
#ifdef _WIN32
    STARTUPINFOA info{};
    info.cb = sizeof(info);
    info.dwFlags = STARTF_USESTDHANDLES;
    SECURITY_ATTRIBUTES attributes{};
    attributes.nLength = sizeof(attributes);
    attributes.bInheritHandle = TRUE;
    // The child's stdin is a pipe the parent writes into; its stdout is a
    // pipe the parent reads from. Two pipes, and the ends the parent keeps are
    // marked non-inheritable so the child cannot see its own writing end.
    HANDLE child_stdin_read = nullptr;
    HANDLE parent_stdin_write = nullptr;
    HANDLE child_stdout_write = nullptr;
    HANDLE parent_stdout_read = nullptr;
    if (!CreatePipe(&child_stdin_read, &parent_stdin_write, &attributes, 0) ||
      !CreatePipe(&parent_stdout_read, &child_stdout_write, &attributes, 0))
    {
      std::printf("  FAIL cannot create the pipes\n");
      ++g_failures;
      return;
    }
    SetHandleInformation(parent_stdin_write, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(parent_stdout_read, HANDLE_FLAG_INHERIT, 0);
    info.hStdInput = child_stdin_read;
    info.hStdOutput = child_stdout_write;
    info.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    std::string command = "\"" + executable + "\"";
    std::vector<char> mutable_command(command.begin(), command.end());
    mutable_command.push_back('\0');
    BOOL started = CreateProcessA(
      nullptr, mutable_command.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &info, &output_);
    process_ = started ? output_.hProcess : nullptr;
    // The child has its own copies now; the parent keeps only its two ends.
    CloseHandle(child_stdin_read);
    CloseHandle(child_stdout_write);
    input_ = parent_stdin_write;
    output_read_ = parent_stdout_read;
#else
    int in_pipe[2];
    int out_pipe[2];
    if (pipe(in_pipe) != 0 || pipe(out_pipe) != 0) {
      std::printf("  FAIL cannot create the pipes\n");
      ++g_failures;
      return;
    }
    pid_ = fork();
    if (pid_ == 0) {
      // The child's stdin is the read end of the first pipe, its stdout the
      // write end of the second. `dup2` first, then close the originals -
      // but only the ones that are not the standard descriptors themselves:
      // a pipe end that happens to *be* fd 1 would be closed by the plain
      // `close` below, and the child would lose its stdout.
      if (in_pipe[0] != STDIN_FILENO) {
        dup2(in_pipe[0], STDIN_FILENO);
        close(in_pipe[0]);
      }
      if (out_pipe[1] != STDOUT_FILENO) {
        dup2(out_pipe[1], STDOUT_FILENO);
        close(out_pipe[1]);
      }
      if (in_pipe[1] > STDERR_FILENO) {close(in_pipe[1]);}
      if (out_pipe[0] > STDERR_FILENO) {close(out_pipe[0]);}
      execl(executable.c_str(), executable.c_str(), nullptr);
      _exit(127);
    }
    if (pid_ < 0) {
      std::printf("  FAIL cannot fork\n");
      ++g_failures;
      return;
    }
    close(in_pipe[0]);
    close(out_pipe[1]);
    input_ = in_pipe[1];
    output_read_ = out_pipe[0];
#endif
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
#endif
  }

  ~Sidecar()
  {
#ifdef _WIN32
    if (process_ != nullptr) {
      TerminateProcess(process_, 0);
      WaitForSingleObject(process_, 2000);
      CloseHandle(process_);
    }
    if (input_ != nullptr) {CloseHandle(input_);}
    if (output_read_ != nullptr) {CloseHandle(output_read_);}
#else
    if (pid_ > 0) {
      // SIGTERM first, then SIGKILL: a child that ignores SIGTERM must not
      // leave the test hanging in `waitpid` until ctest's own timeout.
      kill(pid_, SIGTERM);
      for (int i = 0; i < 20; ++i) {
        int status = 0;
        const pid_t done = waitpid(pid_, &status, WNOHANG);
        if (done == pid_ || done < 0) {
          pid_ = -1;
          break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
      }
      if (pid_ > 0) {
        kill(pid_, SIGKILL);
        waitpid(pid_, nullptr, 0);
        pid_ = -1;
      }
    }
    if (input_ >= 0) {close(input_);}
    if (output_read_ >= 0) {close(output_read_);}
#endif
  }

  bool send(const json & command)
  {
    const std::string body = command.dump();
    std::uint32_t length = static_cast<std::uint32_t>(body.size());
    std::uint8_t header[5] = {
      static_cast<std::uint8_t>(length & 0xff),
      static_cast<std::uint8_t>((length >> 8) & 0xff),
      static_cast<std::uint8_t>((length >> 16) & 0xff),
      static_cast<std::uint8_t>((length >> 24) & 0xff),
      1,
    };
#ifdef _WIN32
    DWORD written = 0;
    if (!WriteFile(input_, header, sizeof(header), &written, nullptr)) {
      return false;
    }
    if (!WriteFile(input_, body.data(), length, &written, nullptr)) {
      return false;
    }
#else
    if (write(input_, header, sizeof(header)) != static_cast<ssize_t>(sizeof(header))) {
      return false;
    }
    if (write(input_, body.data(), length) != static_cast<ssize_t>(length)) {
      return false;
    }
#endif
    return true;
  }

  // Reads one frame. Returns false when nothing arrives within the budget.
  bool receive(json & out, int timeout_ms = 10000)
  {
    std::uint8_t header[5];
    if (!read_exact(header, sizeof(header), timeout_ms)) {
      return false;
    }
    const std::uint32_t length =
      static_cast<std::uint32_t>(header[0]) |
      (static_cast<std::uint32_t>(header[1]) << 8) |
      (static_cast<std::uint32_t>(header[2]) << 16) |
      (static_cast<std::uint32_t>(header[3]) << 24);
    std::vector<std::uint8_t> payload(length);
    if (!read_exact(payload.data(), payload.size(), timeout_ms)) {
      return false;
    }
    try {
      out = json::parse(payload.begin(), payload.end());
    } catch (const std::exception &) {
      return false;
    }
    return true;
  }

  // Waits for the event with that name, ignoring the frames in between.
  bool wait_event(const std::string & name, json & out, int timeout_ms = 15000)
  {
    const auto deadline = std::chrono::steady_clock::now() +
      std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
      json frame;
      if (!receive(frame, 1000)) {
        continue;
      }
      if (frame.value("event", "") == name) {
        out = frame;
        return true;
      }
    }
    return false;
  }

  // Waits for the answer to a command id.
  bool wait_reply(std::uint64_t id, json & out, int timeout_ms = 15000)
  {
    const auto deadline = std::chrono::steady_clock::now() +
      std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
      json frame;
      if (!receive(frame, 1000)) {
        continue;
      }
      if (frame.value("id", 0ULL) == id) {
        out = frame;
        return true;
      }
    }
    return false;
  }

private:
  bool read_exact(std::uint8_t * data, std::size_t size, int timeout_ms)
  {
    std::size_t got = 0;
    const auto deadline = std::chrono::steady_clock::now() +
      std::chrono::milliseconds(timeout_ms);
    while (got < size) {
      const auto now = std::chrono::steady_clock::now();
      if (now >= deadline) {
        return false;
      }
      const int remaining_ms = static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count()) + 1;
#ifdef _WIN32
      // The Windows pipe read is blocking; the ctest timeout is the only
      // backstop there. The Linux side polls, which is what keeps a dead
      // child from hanging the whole test.
      DWORD read = 0;
      if (!ReadFile(output_read_, data + got, static_cast<DWORD>(size - got), &read, nullptr)) {
        return false;
      }
      if (read == 0) {
        return false;
      }
      got += read;
#else
      struct pollfd waiter{};
      waiter.fd = output_read_;
      waiter.events = POLLIN;
      const int ready = poll(&waiter, 1, remaining_ms);
      if (ready == 0) {
        return false;  // the budget ran out
      }
      if (ready < 0) {
        return false;  // a real error, or EINTR we do not retry
      }
      const ssize_t read = ::read(output_read_, data + got, size - got);
      if (read == 0) {
        return false;  // the child closed its end: it is gone
      }
      if (read < 0) {
        return false;
      }
      got += static_cast<std::size_t>(read);
#endif
    }
    return true;
  }

#ifdef _WIN32
  HANDLE process_ = nullptr;
  HANDLE input_ = nullptr;
  HANDLE output_read_ = nullptr;
  PROCESS_INFORMATION output_{};
#else
  pid_t pid_ = -1;
  int input_ = -1;
  int output_read_ = -1;
#endif
};

}  // namespace

int main(int argc, char ** argv)
{
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  if (argc < 2) {
    std::printf("usage: bridge_viz_feed_action <path to viz_feed>\n");
    return 2;
  }
  const std::string executable = argv[1];

  // --- the goal that is carried out ------------------------------------
  {
    FakeNavigator navigator(false);
    Sidecar sidecar(executable);
    sidecar.send(json{{"op", "hello"}, {"id", 1}});

    json reply;
    check(sidecar.wait_reply(1, reply), "the sidecar answers hello");
    check(reply.is_object() && reply.value("ok", false), "hello succeeded");

    sidecar.send(json{{"op", "navigate_to_pose"}, {"id", 2},
      {"x", 2.5}, {"y", 1.8}, {"yaw", 0.5}, {"frame", "map"}});
    check(sidecar.wait_reply(2, reply), "the sidecar answers navigate_to_pose");
    check(reply.is_object() && reply.value("ok", false), "the goal was accepted for sending");
    const auto goal_id =
      reply.is_object() && reply.contains("result") ? reply["result"].value("goal_id", 0ULL) : 0ULL;
    check(goal_id != 0, "the goal got an id");

    json status;
    check(sidecar.wait_event("goal_status", status), "a goal_status event arrives");
    check(
      status.value("data", json::object()).value("state", "") == "accepted",
      "the goal_status event says the goal was accepted");

    json feedback;
    check(sidecar.wait_event("goal_feedback", feedback), "a goal_feedback event arrives");
    const std::string text = feedback.value("data", json::object()).value("text", "");
    check(text.find("distance_remaining") != std::string::npos,
      "the feedback carries the remaining distance");

    json result;
    check(sidecar.wait_event("goal_result", result), "a goal_result event arrives");
    check(
      result.value("data", json::object()).value("state", "") == "succeeded",
      "the goal_result event says the goal succeeded");
    check(result.value("goal_id", 0ULL) == goal_id, "the result belongs to the goal");

    check(navigator.goals() == 1, "the server saw exactly one goal");
    check(navigator.last_x() == 2.5, "the server got the x the GUI asked for");
    check(navigator.last_y() == 1.8, "the server got the y the GUI asked for");

    sidecar.send(json{{"op", "shutdown"}, {"id", 3}});
  }

  // --- the goal that is canceled ---------------------------------------
  {
    FakeNavigator navigator(true);
    Sidecar sidecar(executable);
    sidecar.send(json{{"op", "hello"}, {"id", 1}});
    json reply;
    sidecar.wait_reply(1, reply);

    sidecar.send(json{{"op", "navigate_to_pose"}, {"id", 2}, {"x", 1.0}, {"y", 1.0}});
    check(sidecar.wait_reply(2, reply), "the sidecar accepts the second goal");
    json status;
    check(sidecar.wait_event("goal_status", status), "the second goal is accepted too");

    sidecar.send(json{{"op", "cancel_goal"}, {"id", 3}});
    check(sidecar.wait_reply(3, reply), "the sidecar answers cancel_goal");
    check(
      reply.is_object() && reply.contains("result") &&
      reply["result"].value("cancelled", false),
      "the cancel was accepted");

    json result;
    check(sidecar.wait_event("goal_result", result), "the canceled goal ends");
    check(
      result.value("data", json::object()).value("state", "") == "canceled",
      "the goal ended as canceled");
    check(navigator.cancels() == 1, "the server saw the cancel");
  }

  if (g_failures == 0) {
    std::printf("bridge_viz_feed_action: %d checks passed\n", g_checks);
    return 0;
  }
  std::printf("bridge_viz_feed_action: %d/%d checks failed\n", g_failures, g_checks);
  return 1;
}
