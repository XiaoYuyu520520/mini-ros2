// Compile-time validation of the rclcpp compatibility shim headers.
#include "bondcpp/bond.hpp"
#include "pluginlib/class_loader.hpp"
#include "pluginlib/class_list_macros.hpp"
#include "rclcpp/rclcpp.hpp"
#include "rclcpp_lifecycle/lifecycle_node.hpp"
#include "std_msgs/msg/string.hpp"

#include <memory>

namespace
{

class DemoBase
{
public:
  virtual ~DemoBase() = default;
  virtual int value() const = 0;
};

class DemoPlugin : public DemoBase
{
public:
  int value() const override {return 42;}
};

class DemoLifecycleNode : public rclcpp_lifecycle::LifecycleNode
{
public:
  explicit DemoLifecycleNode(const rclcpp::NodeOptions & options = rclcpp::NodeOptions())
  : rclcpp_lifecycle::LifecycleNode("demo", "", options, true)
  {
  }

  rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn
  on_configure(const rclcpp_lifecycle::State &) override
  {
    return rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn::SUCCESS;
  }
};

}  // namespace

PLUGINLIB_EXPORT_CLASS(DemoPlugin, DemoBase)

int main()
{
  pluginlib::ClassLoader<DemoBase> loader("demo", "DemoBase");
  auto plugin = loader.createSharedInstance("demo/DemoPlugin");
  if (!plugin || plugin->value() != 42) {
    return 1;
  }

  rclcpp::init(0, nullptr);
  auto node = std::make_shared<rclcpp_lifecycle::LifecycleNode>("cnode");
  auto publisher = node->create_publisher<std_msgs::msg::String>("compile_topic", rclcpp::QoS(10));
  if (publisher->get_topic_name() == nullptr) {
    return 2;
  }
  node->configure();
  node->activate();
  node->deactivate();
  node->cleanup();
  rclcpp::shutdown();
  return 0;
}
