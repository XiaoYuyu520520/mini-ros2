#ifndef INTERACTIVE_MARKERS__MENU_HANDLER_HPP_
#define INTERACTIVE_MARKERS__MENU_HANDLER_HPP_

// Menu support for interactive markers is not implemented in the shim: none of
// the ported packages add menu entries to their interactive markers, and the
// entry point that would consume them (an RViz click) is out of scope for a
// headless Windows process. The type is provided so that packages can include
// the upstream header and keep a MenuHandler member around.

#include <cstdint>
#include <string>

#include "visualization_msgs/msg/interactive_marker.hpp"
#include "visualization_msgs/msg/menu_entry.hpp"

namespace interactive_markers
{

using MenuEntry = visualization_msgs::msg::MenuEntry;

class MenuHandler
{
public:
  using EntryHandle = std::uint32_t;

  EntryHandle insert(const MenuEntry &) {return next_handle_++;}
  void apply(visualization_msgs::msg::InteractiveMarker &) {}
  void reApply(visualization_msgs::msg::InteractiveMarker &) {}
  EntryHandle getFirstEntryHandle() {return 0;}

private:
  EntryHandle next_handle_ = 1;
};

}  // namespace interactive_markers

#endif  // INTERACTIVE_MARKERS__MENU_HANDLER_HPP_
