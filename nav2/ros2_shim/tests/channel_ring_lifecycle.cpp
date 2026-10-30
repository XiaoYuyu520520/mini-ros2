// The channel ring keeps what its previous writer left, and a reader that turns
// up later must not be handed any of it.
//
// That is the rule `ShmChannel::seek_to_latest()` exists for, and it was
// missing from two of the shim's four readers:
//
//   `Subscription`                        (rclcpp/subscription.hpp)   seeks
//   `rclcpp_action::Client` / `Server`    (rclcpp_action/...)         seek
//   `rclcpp::Service`                     (rclcpp/service.hpp)  did not
//   `rclcpp::Client`                      (rclcpp/client.hpp)   did not
//
// What the two that did not cost: `slam_toolbox` recreates
// `slam_toolbox/serialize_map` every time it activates, so a second node of that
// name *replayed* the `serialize_map` request the first one had already
// answered, answered it again out of an empty graph, and overwrote the 28 MB
// archive it was about to deserialize (03-validation.md, evidence 41). The
// client half is the mirror image: a client numbers its requests from 1, so a
// response left in the ring by an earlier client is read back as this client's
// first answer.
//
// Nothing here throws or logs when it goes wrong - the reader simply starts one
// stream behind and consumes somebody else's message - so the failure has to be
// pinned by a test rather than noticed. This is that test, and it is deliberate
// that every case is a *restart* of a name: the very first server of a name
// cannot be handed a leftover, because there is nothing in the ring yet.
//
// A restart only leaves a leftover behind if the ring is still there when the
// replacement arrives, and that is a property of the *section*, not of the
// server: a channel is a `Local\` named section and Windows destroys it with
// its last handle, so a server whose client is still running - or whose other
// end is another process, a bridge daemon, a `ros2 service call` - finds the
// ring where it left it, while a server that was the only holder finds a
// brand new one and cannot replay anything whatever its constructor does. Both
// halves are pinned below: `test_ring_outlives_its_writer` for the mechanism,
// and the two restart tests for the rule. They hold the channel open across the
// node they are replacing on purpose, and dropping that holder is the one edit
// that would turn them green again without fixing anything.
//
//   test_ring_keeps_its_leftovers    the premise, with the control that keeps
//                                    the rest of the file from being vacuous: a
//                                    reader that does *not* seek really does see
//                                    the whole ring
//   test_constructors_skip_the_ring  the two constructors directly, so the
//                                    assertion points at the lines that fix it
//                                    rather than at `create_service()`
//   test_second_server_of_the_name   the reported bug, end to end
//   test_second_client_of_the_name   the mirror, end to end
//   test_client_before_server        ordering: a request published before the
//                                    server existed is dropped, not delivered
//                                    late (the volatile durability reading)
//   test_many_requests_at_once       three requests in flight from a fresh
//                                    client, so ids and payloads have to match
//   test_repeated_restarts           the same thing fifty times over, because
//                                    one restart can pass by luck
#include "rclcpp/rclcpp.hpp"
#include "rclcpp/shm_config.hpp"
#include "ros2_shim/serialization.hpp"
#include "shm_transport/shm_channel.hpp"

#include "std_srvs/srv/set_bool.hpp"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <future>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace
{

int g_checks = 0;
int g_failures = 0;

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

// The same thing without a line per success: the restart test makes hundreds of
// these and only the failures are worth reading.
void check_quiet(bool ok, const std::string & what)
{
  ++g_checks;
  if (!ok) {
    ++g_failures;
    std::printf("  FAIL %s\n", what.c_str());
  }
}

using SetBool = std_srvs::srv::SetBool;
using Request = SetBool::Request;
using Response = SetBool::Response;
using SetBoolClient = rclcpp::Client<SetBool>;

// `set_bool` is the smallest service the shim has that carries a string back:
// the answer can be signed, so a caller can tell one server of a name from the
// next one, which is what every test below needs to prove whose answer it got.
std::shared_ptr<shm_transport::ShmChannel> open_or_create(
  const std::string & channel_name, uint32_t slot_size)
{
  std::shared_ptr<shm_transport::ShmChannel> channel =
    shm_transport::ShmChannel::open(channel_name);
  if (!channel) {
    channel = shm_transport::ShmChannel::create(
      channel_name, rclcpp::shim::default_slot_count(), slot_size);
  }
  return channel;
}

// Both halves of a service are sized from the *service* name - that is what
// `Node::create_service` does and what the bridge's CLI helper does - so a raw
// writer has to size them the same way, or it would create a second, mismatched
// channel instead of joining the one the node is using.
std::shared_ptr<shm_transport::ShmChannel> open_service_half(
  const std::string & service, const char * half)
{
  return open_or_create(
    rclcpp::shim::channel_name(service + half),
    rclcpp::shim::slot_size_for_topic(service));
}

void publish_bytes(
  const std::shared_ptr<shm_transport::ShmChannel> & channel, uint64_t id,
  const std::vector<uint8_t> & payload)
{
  std::vector<uint8_t> out(sizeof(id) + payload.size());
  std::memcpy(out.data(), &id, sizeof(id));
  std::memcpy(out.data() + sizeof(id), payload.data(), payload.size());
  channel->publish(out.data(), out.size());
}

// One request in the ring, written exactly the way `Client::async_send_request`
// writes it, minus the client: this stands in for a caller that asked and then
// went away, which is what the ring still holds after a server answered it.
void publish_orphan_request(const std::string & service, uint64_t id, bool data)
{
  Request request;
  request.data = data;
  publish_bytes(open_service_half(service, "/_request"), id, ros2_shim::serialize(request));
}

// A server that remembers what it was asked and signs its answers, so a caller
// can tell it apart from the previous server of the same name.
struct Server
{
  std::vector<bool> requests;
  std::string tag;

  int callbacks() const {return static_cast<int>(requests.size());}
};

std::shared_ptr<Server> serve(
  rclcpp::Node & node, const std::string & service, const std::string & tag)
{
  auto server = std::make_shared<Server>();
  server->tag = tag;
  node.create_service<SetBool>(
    service,
    [server](const std::shared_ptr<Request> request, std::shared_ptr<Response> response) {
      server->requests.push_back(request->data);
      response->success = true;
      response->message = server->tag + ":" + (request->data ? "true" : "false");
    });
  return server;
}

void pump(rclcpp::Node & server_node, rclcpp::Node & client_node, int iterations, int sleep_ms)
{
  for (int i = 0; i < iterations; ++i) {
    server_node.spin_once();
    client_node.spin_once();
    std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
  }
}

// Send one request and spin both nodes until it is answered, which is what a
// caller does while it waits. nullptr means it never was.
std::shared_ptr<Response> call(
  rclcpp::Node & server_node, rclcpp::Node & client_node,
  const std::shared_ptr<SetBoolClient> & client, bool data, int iterations = 3000)
{
  auto request = std::make_shared<Request>();
  request->data = data;
  auto future = client->async_send_request(request);
  for (int i = 0;
    i < iterations && future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready;
    ++i)
  {
    server_node.spin_once();
    client_node.spin_once();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  if (future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
    return nullptr;
  }
  return future.get();
}

// --------------------------------------------------------------- the premise

void test_ring_keeps_its_leftovers()
{
  std::printf("[test] the ring keeps what its previous writer left\n");
  const std::string channel_name = rclcpp::shim::channel_name("/ring_leftover/raw");
  const uint32_t slot_size = rclcpp::shim::default_slot_size();

  auto writer = open_or_create(channel_name, slot_size);
  for (int i = 0; i < 5; ++i) {
    const uint8_t byte = static_cast<uint8_t>(0x10 + i);
    check(writer->publish(&byte, 1), "the writer published a byte");
  }

  // The control, and the reason the rest of this file is not vacuous: a reader
  // that does *not* seek really is handed all five. Without this, a "nothing
  // was delivered" assertion elsewhere could pass on a ring that never held
  // anything in the first place.
  std::vector<uint8_t> buffer(64);
  size_t size = 0;
  int seen = 0;
  auto naive = open_or_create(channel_name, slot_size);
  while (naive->subscribe(buffer.data(), buffer.size(), &size, 0)) {
    ++seen;
  }
  check(seen == 5, "a reader that does not seek is fed the whole ring (control)");

  // The rule itself.
  auto fresh = open_or_create(channel_name, slot_size);
  check(
    fresh->seek_to_latest() == 5,
    "seek_to_latest() reports the five messages it skipped");
  check(
    !fresh->subscribe(buffer.data(), buffer.size(), &size, 0),
    "a reader that seeks is not fed the leftovers");

  // ...and it is a reader, not a black hole: the next message does arrive.
  const uint8_t next = 0x99;
  check(writer->publish(&next, 1), "the writer published one more byte");
  const bool got = fresh->subscribe(buffer.data(), buffer.size(), &size, 0);
  check(
    got && size == 1 && buffer[0] == next,
    "the same reader still sees the next message");
}

// ---------------------------------------------- the ring outlives its writer

void test_ring_outlives_its_writer()
{
  std::printf("[test] a ring outlives the writer that filled it, while anyone holds it\n");
  const uint32_t slot_size = rclcpp::shim::default_slot_size();
  std::vector<uint8_t> buffer(64);
  size_t size = 0;

  // Someone else's handle - a client that has not given up yet, a daemon, a
  // second node - is what keeps the section (and its history) alive after the
  // writer is gone.
  {
    auto keeper = open_or_create(rclcpp::shim::channel_name("/ring_kept/raw"), slot_size);
    {
      auto writer = open_or_create(rclcpp::shim::channel_name("/ring_kept/raw"), slot_size);
      const uint8_t byte = 0x5A;
      check(writer->publish(&byte, 1), "a writer filled a channel that somebody else holds");
    }

    auto reader = open_or_create(rclcpp::shim::channel_name("/ring_kept/raw"), slot_size);
    const bool got = reader->subscribe(buffer.data(), buffer.size(), &size, 0);
    check(
      got && size == 1 && buffer[0] == 0x5A,
      "the writer is gone and the message is still there for a later reader");

    auto late = open_or_create(rclcpp::shim::channel_name("/ring_kept/raw"), slot_size);
    check(
      late->seek_to_latest() == 1,
      "so a reader joining now does have something to skip");
  }

  // The other half, so the holders above are not read as superstition: with no
  // handle left at all the section goes with its last one, and a channel built
  // again from nothing has no history to replay.
  {
    auto writer = open_or_create(rclcpp::shim::channel_name("/ring_unkept/raw"), slot_size);
    const uint8_t byte = 0x5A;
    check(writer->publish(&byte, 1), "a writer filled a channel nobody else holds");
  }
  auto recreated = open_or_create(rclcpp::shim::channel_name("/ring_unkept/raw"), slot_size);
  check(
    !recreated->subscribe(buffer.data(), buffer.size(), &size, 0),
    "with no holder left the section is gone, so there is nothing to be handed");
  check(
    recreated->published_count() == 0,
    "and the channel built in its place counts no messages");
}

// --------------------------------------------------- the two constructors direct

void test_constructors_skip_the_ring()
{
  std::printf("[test] rclcpp::Service and rclcpp::Client skip the ring as they are built\n");
  const std::string service = "/ring_ctor/svc";
  const uint32_t slot_size = rclcpp::shim::slot_size_for_topic(service);

  // Two requests from a server that is gone, and one answer from a client that
  // is gone - all three still in their rings.
  publish_orphan_request(service, 11, false);
  publish_orphan_request(service, 12, true);
  auto response_channel = open_service_half(service, "/_response");
  {
    Response stale;
    stale.success = true;
    stale.message = "from a previous client";
    publish_bytes(response_channel, 1, ros2_shim::serialize(stale));
  }

  int calls = 0;
  auto request_channel = open_service_half(service, "/_request");
  auto service_object = std::make_shared<rclcpp::Service<SetBool>>(
    service, request_channel, response_channel,
    [&calls](
      std::shared_ptr<rmw_request_id_t>, std::shared_ptr<Request>,
      std::shared_ptr<Response> response) {
      ++calls;
      response->success = true;
      response->message = "answered";
    },
    slot_size);
  service_object->poll();
  check(
    calls == 0,
    "rclcpp::Service's constructor skips what the request ring already holds");

  auto client = std::make_shared<SetBoolClient>(
    service, request_channel, response_channel, slot_size);
  auto request = std::make_shared<Request>();
  request->data = true;
  auto future = client->async_send_request(request);
  // This is the first request of this client, so its id is 1 - which is exactly
  // the correlation id the leftover answer carries.
  client->poll();
  check(
    future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready,
    "rclcpp::Client's constructor skips the leftover answer");

  // The two are still wired to each other, so "nothing was delivered" above is
  // not the shim having quietly stopped working.
  bool answered = false;
  for (int i = 0; i < 2000 && !answered; ++i) {
    service_object->poll();
    client->poll();
    answered = future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready;
    if (!answered) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }
  check(answered, "a request sent after the constructors is answered");
  if (answered) {
    check(calls == 1, "and it is the only request this server was handed");
    check(future.get()->message == "answered", "with this server's answer");
  }
}

// ------------------------------------------------- a second server of a name

void test_second_server_of_the_name()
{
  std::printf("[test] a second server of the same name is not handed the first one's requests\n");
  const std::string service = "/ring_second_server/svc";

  // The caller that stays up across the restart, which is what makes the two
  // requests below outlive the server that answered the first of them.
  auto request_keeper = open_service_half(service, "/_request");
  auto response_keeper = open_service_half(service, "/_response");

  {
    rclcpp::Node node("ring_second_server_1");
    auto first = serve(node, service, "first");
    rclcpp::Node client_node("ring_second_server_c1");
    auto client = client_node.create_client<SetBool>(service);

    const auto response = call(node, client_node, client, false);
    check(
      response && response->message == "first:false",
      "the first server answered its own request");
    check(first->callbacks() == 1, "the first server served exactly one request");

    // And one more request whose caller is not around to read the answer. Both
    // are in the ring when this scope ends, and the server that replaces this
    // node must not be handed either of them.
    publish_orphan_request(service, 7, true);
    check(
      first->callbacks() == 1,
      "a request published while nothing polls is not served late");
  }

  {
    rclcpp::Node node("ring_second_server_2");
    auto second = serve(node, service, "second");
    rclcpp::Node client_node("ring_second_server_c2");
    pump(node, client_node, 40, 2);
    check(
      second->callbacks() == 0,
      "the second server was handed neither of the previous requests");

    auto client = client_node.create_client<SetBool>(service);
    const auto response = call(node, client_node, client, true);
    check(
      response && response->message == "second:true",
      "the second server still answers a request of its own");
    check(second->callbacks() == 1, "and it served exactly that one");
  }
}

// ------------------------------------------------- a second client of a name

void test_second_client_of_the_name()
{
  std::printf("[test] a second client of the same name does not read the first one's answer\n");
  const std::string service = "/ring_second_client/svc";

  // The answer the first client read is still in this ring when the second one
  // opens it, and the second server's own answer - "second:false" - is what
  // proves which of the two it was handed.
  auto request_keeper = open_service_half(service, "/_request");
  auto response_keeper = open_service_half(service, "/_response");

  {
    rclcpp::Node node("ring_second_client_s1");
    auto first = serve(node, service, "first");
    rclcpp::Node client_node("ring_second_client_c1");
    auto client = client_node.create_client<SetBool>(service);

    const auto response = call(node, client_node, client, false);
    check(
      response && response->message == "first:false",
      "the first client got its own answer");
    check(first->callbacks() == 1, "the first server served exactly once");
  }
  // That answer went out with correlation id 1 and is still in the ring.

  {
    rclcpp::Node node("ring_second_client_s2");
    auto second = serve(node, service, "second");
    rclcpp::Node client_node("ring_second_client_c2");
    auto client = client_node.create_client<SetBool>(service);

    // Request ids restart at 1 in every client, so the leftover answer is
    // precisely the one a client that did not skip the ring would read back as
    // the answer to its first request - and the request it just sent would be
    // reported as answered by a server that never saw it.
    const auto response = call(node, client_node, client, true);
    check(response != nullptr, "the second client got an answer");
    check(
      response && response->message == "second:true",
      "and it is this server's answer, not the previous client's");
    check(second->callbacks() == 1, "the second server served exactly once");
  }
}

// ------------------------------------------------------------- start ordering

void test_client_before_server()
{
  std::printf("[test] a request published before the server existed is dropped, not delivered\n");
  const std::string service = "/ring_order/svc";

  // The client first. `create_client` creates both halves of the channel, so
  // this request goes into a ring no server is reading yet.
  rclcpp::Node client_node("ring_order_c");
  auto client = client_node.create_client<SetBool>(service);
  auto request = std::make_shared<Request>();
  request->data = false;
  auto early = client->async_send_request(request);
  client_node.spin_once();

  // Now the server. Volatile durability means a request that predates it is
  // gone for good: upstream rclcpp hands it to an empty DDS void and the caller
  // gets nothing back, which is why every nav2 caller waits for the service
  // first. What must not happen is the opposite of that - answering a request
  // whose caller has already moved on.
  rclcpp::Node server_node("ring_order_s");
  auto server = serve(server_node, service, "late");
  pump(server_node, client_node, 40, 2);
  check(
    server->callbacks() == 0,
    "the server was not handed the request that predates it");
  check(
    early.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready,
    "and that caller is still waiting rather than holding a late answer");

  // A request sent now is served normally.
  auto late = client->async_send_request(request);
  bool answered = false;
  for (int i = 0; i < 2000 && !answered; ++i) {
    server_node.spin_once();
    client_node.spin_once();
    answered = late.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready;
    if (!answered) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }
  check(answered, "a request sent after the server is up is answered");
  if (answered) {
    check(server->callbacks() == 1, "and the server served exactly that one");
    check(late.get()->message == "late:false", "with this server's answer");
  }
}

// ------------------------------------------------------ several at once

void test_many_requests_at_once()
{
  std::printf("[test] a fresh client's requests are matched to their own answers\n");
  const std::string service = "/ring_multi/svc";
  rclcpp::Node server_node("ring_multi_s");
  auto server = serve(server_node, service, "multi");
  rclcpp::Node client_node("ring_multi_c");
  auto client = client_node.create_client<SetBool>(service);

  // Three in flight before any answer is read. Their ids are 1, 2 and 3, so a
  // leftover answer in the ring answers the first of them with a stranger's
  // response and leaves the other two to time out.
  std::vector<rclcpp::FutureAndRequestId<std::shared_ptr<Response>>> futures;
  const bool payloads[3] = {false, true, false};
  for (int i = 0; i < 3; ++i) {
    auto request = std::make_shared<Request>();
    request->data = payloads[i];
    futures.push_back(client->async_send_request(request));
  }

  int ready = 0;
  for (int i = 0; i < 3000 && ready < 3; ++i) {
    server_node.spin_once();
    client_node.spin_once();
    ready = 0;
    for (auto & future : futures) {
      if (future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
        ++ready;
      }
    }
    if (ready < 3) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }

  check(ready == 3, "all three requests were answered");
  check(server->callbacks() == 3, "the server was handed three requests, not two or four");
  if (server->callbacks() == 3) {
    check(
      server->requests[0] == false && server->requests[1] == true &&
      server->requests[2] == false,
      "the three payloads arrived in the order they were sent");
  }
}

// ------------------------------------------------------- and fifty times over

void test_repeated_restarts(int rounds)
{
  std::printf("[test] %d consecutive restarts of one service name\n", rounds);
  const std::string service = "/ring_restart/svc";
  // One holder for all `rounds`: without it the ring would be destroyed with
  // each round's node and "no round started with an earlier round's request"
  // would hold for the wrong reason.
  auto request_keeper = open_service_half(service, "/_request");
  auto response_keeper = open_service_half(service, "/_response");
  int stale_deliveries = 0;
  int missed_answers = 0;
  int wrong_answers = 0;

  for (int round = 1; round <= rounds; ++round) {
    const std::string suffix = std::to_string(round);
    const bool data = (round % 2) == 0;

    rclcpp::Node server_node("ring_restart_s" + suffix);
    auto server = serve(server_node, service, "round" + suffix);
    rclcpp::Node client_node("ring_restart_c" + suffix);
    auto client = client_node.create_client<SetBool>(service);

    // Nothing has been sent in this round yet, so nothing may be delivered:
    // every request still in the ring belongs to an earlier round, whose server
    // has already answered it and gone.
    pump(server_node, client_node, 12, 1);
    if (server->callbacks() != 0) {
      ++stale_deliveries;
    }

    const auto response = call(server_node, client_node, client, data);
    if (!response) {
      ++missed_answers;
      continue;
    }
    if (server->callbacks() != 1) {
      ++wrong_answers;
    } else if (response->message != "round" + suffix + (data ? ":true" : ":false")) {
      ++wrong_answers;
    }
  }

  check_quiet(
    stale_deliveries == 0,
    "no round of " + std::to_string(rounds) + " started with a request from an earlier round");
  check_quiet(
    missed_answers == 0,
    "every round of " + std::to_string(rounds) + " had its request answered");
  check_quiet(
    wrong_answers == 0,
    "every round was served exactly once, with its own answer");
  check(
    stale_deliveries == 0 && missed_answers == 0 && wrong_answers == 0,
    "restarting a service name " + std::to_string(rounds) +
    " times is clean (stale " + std::to_string(stale_deliveries) +
    ", unanswered " + std::to_string(missed_answers) +
    ", wrong " + std::to_string(wrong_answers) + ")");
}

}  // namespace

int main()
{
  std::printf("=== channel rings: who is handed the leftovers ===\n");
  rclcpp::init(0, nullptr);

  test_ring_keeps_its_leftovers();
  test_ring_outlives_its_writer();
  test_constructors_skip_the_ring();
  test_second_server_of_the_name();
  test_second_client_of_the_name();
  test_client_before_server();
  test_many_requests_at_once();
  test_repeated_restarts(50);

  rclcpp::shutdown();

  std::printf(
    "\n%d checks, %d failure(s)%s\n", g_checks, g_failures,
    g_failures == 0 ? " - ALL CHECKS PASSED" : "");
  return g_failures == 0 ? 0 : 1;
}
