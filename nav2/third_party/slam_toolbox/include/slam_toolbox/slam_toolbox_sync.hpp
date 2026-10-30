/*
 * slam_toolbox
 * Copyright Work Modifications (c) 2018, Simbe Robotics, Inc.
 * Copyright Work Modifications (c) 2019, Steve Macenski
 *
 * THE WORK (AS DEFINED BELOW) IS PROVIDED UNDER THE TERMS OF THIS CREATIVE
 * COMMONS PUBLIC LICENSE ("CCPL" OR "LICENSE"). THE WORK IS PROTECTED BY
 * COPYRIGHT AND/OR OTHER APPLICABLE LAW. ANY USE OF THE WORK OTHER THAN AS
 * AUTHORIZED UNDER THIS LICENSE OR COPYRIGHT LAW IS PROHIBITED.
 *
 * BY EXERCISING ANY RIGHTS TO THE WORK PROVIDED HERE, YOU ACCEPT AND AGREE TO
 * BE BOUND BY THE TERMS OF THIS LICENSE. THE LICENSOR GRANTS YOU THE RIGHTS
 * CONTAINED HERE IN CONSIDERATION OF YOUR ACCEPTANCE OF SUCH TERMS AND
 * CONDITIONS.
 *
 */

/* Author: Steven Macenski */

#ifndef SLAM_TOOLBOX__SLAM_TOOLBOX_SYNC_HPP_
#define SLAM_TOOLBOX__SLAM_TOOLBOX_SYNC_HPP_

#include <queue>
#include <memory>
#include "slam_toolbox/slam_toolbox_common.hpp"

namespace slam_toolbox
{

class SynchronousSlamToolbox : public SlamToolbox
{
public:
  explicit SynchronousSlamToolbox(rclcpp::NodeOptions options);
  // PORT (ros2_shim): interrupt and join the threads here, before the base
  // destructor runs, instead of leaving an empty body as upstream does.
  //
  // `run()` - the thread `on_activate()` starts - locks `q_mutex_` on every
  // iteration, and `q_mutex_` is a member of *this* class. The interrupt and
  // the join live in the base destructor, and a C++ destructor runs derived
  // body, derived members, base body: upstream destroys `q_mutex_` and only
  // afterwards joins the thread that keeps locking it.
  //
  // That window is not a quiet one on glibc. With the thread still running,
  // `pthread_mutex_destroy` fails and boost aborts inside `~mutex`:
  //
  //   boost/thread/pthread/mutex.hpp:56: boost::mutex::~mutex(): Assertion
  //   `!posix::pthread_mutex_destroy(&m)' failed.
  //
  // Measured with the same two arms in `.scratch/dtor_order` (same classes,
  // same order, thread looping through the lock): 10/10 aborts without the
  // join below, 0/10 with it. Windows' pthread implementation does not
  // validate the kind, which is why upstream never saw this - and why the port
  // has to keep the edit rather than trust the platform it was tested on.
  // Registered as nav2/patches/slam_toolbox-thread-shutdown.patch; see the
  // README next to it.
  //
  // Joining here first closes the window; the base destructor then walks an
  // empty vector and does nothing.
  ~SynchronousSlamToolbox()
  {
    for (auto & thread : threads_) {
      thread->interrupt();
      thread->join();
    }
    threads_.clear();
  }
  void run();
  CallbackReturn on_activate(const rclcpp_lifecycle::State & state) override;
  CallbackReturn on_deactivate(const rclcpp_lifecycle::State & state) override;

protected:
  void laserCallback(sensor_msgs::msg::LaserScan::ConstSharedPtr scan) override;
  bool clearQueueCallback(
    const std::shared_ptr<rmw_request_id_t> request_header,
    const std::shared_ptr<slam_toolbox::srv::ClearQueue::Request> req,
    std::shared_ptr<slam_toolbox::srv::ClearQueue::Response> resp);
  bool deserializePoseGraphCallback(
    const std::shared_ptr<rmw_request_id_t> request_header,
    const std::shared_ptr<slam_toolbox::srv::DeserializePoseGraph::Request> req,
    std::shared_ptr<slam_toolbox::srv::DeserializePoseGraph::Response> resp) override;
  bool resetCallback(
    const std::shared_ptr<rmw_request_id_t> request_header,
    const std::shared_ptr<slam_toolbox::srv::Reset::Request> req,
    std::shared_ptr<slam_toolbox::srv::Reset::Response> resp) override;

  std::queue<PosedScan> q_;
  std::shared_ptr<rclcpp::Service<slam_toolbox::srv::ClearQueue>> ssClear_;
  boost::mutex q_mutex_;
};

}  // namespace slam_toolbox

#endif  // SLAM_TOOLBOX__SLAM_TOOLBOX_SYNC_HPP_
