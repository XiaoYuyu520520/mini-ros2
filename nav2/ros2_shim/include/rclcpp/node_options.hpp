#ifndef RCLCPP__NODE_OPTIONS_HPP_
#define RCLCPP__NODE_OPTIONS_HPP_

#include <string>
#include <vector>

#include "rclcpp/parameter.hpp"

namespace rclcpp
{

class NodeOptions
{
public:
  NodeOptions() = default;

  NodeOptions & automatically_declare_parameters_from_overrides(bool value)
  {
    automatically_declare_parameters_from_overrides_ = value;
    return *this;
  }

  bool automatically_declare_parameters_from_overrides() const
  {
    return automatically_declare_parameters_from_overrides_;
  }

  NodeOptions & allow_undeclared_parameters(bool value)
  {
    allow_undeclared_parameters_ = value;
    return *this;
  }

  bool allow_undeclared_parameters() const {return allow_undeclared_parameters_;}

  NodeOptions & use_intra_process_comms(bool value)
  {
    use_intra_process_comms_ = value;
    return *this;
  }

  bool use_intra_process_comms() const {return use_intra_process_comms_;}

  // The ROS arguments this node, and any node created from these options, is
  // given - `{"--ros-args", "--params-file", path, "-p", "name:=value"}`, the
  // form `ros2 run` produces. A node honours the two parameter arguments out of
  // it; the remapping arguments are ignored, because the shim resolves a node's
  // name and namespace from the names it was constructed with.
  NodeOptions & arguments(const std::vector<std::string> & arguments)
  {
    arguments_ = arguments;
    return *this;
  }

  const std::vector<std::string> & arguments() const {return arguments_;}

  NodeOptions & parameter_overrides(const std::vector<rclcpp::Parameter> & overrides)
  {
    parameter_overrides_ = overrides;
    return *this;
  }

  const std::vector<rclcpp::Parameter> & parameter_overrides() const
  {
    return parameter_overrides_;
  }

  NodeOptions & enable_topic_statistics(bool value)
  {
    enable_topic_statistics_ = value;
    return *this;
  }

  bool enable_topic_statistics() const {return enable_topic_statistics_;}

  NodeOptions & start_parameter_services(bool value)
  {
    start_parameter_services_ = value;
    return *this;
  }

  bool start_parameter_services() const {return start_parameter_services_;}

  NodeOptions & start_parameter_event_publisher(bool value)
  {
    start_parameter_event_publisher_ = value;
    return *this;
  }

  bool start_parameter_event_publisher() const {return start_parameter_event_publisher_;}

  // Whether this node publishes its log to `/rosout`. On upstream, where every
  // node has one, `nav2_ros_common::generate_internal_node()` turns it off for
  // the private helper nodes a server builds - each is a `/rosout` publisher and
  // a channel to the other, and nobody reads the log of a node whose whole job
  // is to be reached through its parent.
  //
  // Default true, as upstream: `ros2 topic list` on a real robot shows
  // `/rosout`, and a shim node that does not is a hole in the graph rather than
  // a saving. `NAV2_SHM_ROSOUT=0` turns it off for a whole process.
  NodeOptions & enable_rosout(bool value)
  {
    enable_rosout_ = value;
    return *this;
  }

  bool enable_rosout() const {return enable_rosout_;}

private:
  bool automatically_declare_parameters_from_overrides_ = false;
  bool allow_undeclared_parameters_ = false;
  bool use_intra_process_comms_ = false;
  bool enable_topic_statistics_ = false;
  bool start_parameter_services_ = true;
  bool start_parameter_event_publisher_ = true;
  bool enable_rosout_ = true;
  std::vector<std::string> arguments_;
  std::vector<rclcpp::Parameter> parameter_overrides_;
};

// How a node reads its block out of a parameter file. The implementation
// (`ros2_shim::loadParamsFile`) needs yaml-cpp and lives in the
// ros2_shim_params library; this is the seam that lets the header-only core
// call it anyway. A runner installs it once at startup with
// `ros2_shim::installParameterFileLoader()`, and every node it builds - the
// servers' own costmaps included, which nav2 creates with the parent's
// NodeOptions - then finds its parameters where upstream finds them.
using ParameterFileLoader = bool (*)(
  const std::string & path, const std::string & node_name,
  const std::string & node_namespace, std::vector<Parameter> & out, std::string * error);

inline ParameterFileLoader & parameter_file_loader()
{
  static ParameterFileLoader loader = nullptr;
  return loader;
}

inline void set_parameter_file_loader(ParameterFileLoader loader)
{
  parameter_file_loader() = loader;
}

}  // namespace rclcpp

#endif  // RCLCPP__NODE_OPTIONS_HPP_
