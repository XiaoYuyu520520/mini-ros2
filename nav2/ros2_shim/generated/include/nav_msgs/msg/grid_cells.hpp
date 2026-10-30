#ifndef NAV_MSGS_MSG_GRIDCELLS_HPP_
#define NAV_MSGS_MSG_GRIDCELLS_HPP_

#include "geometry_msgs/msg/point.hpp"
#include "std_msgs/msg/header.hpp"
#include <vector>

#include <cstdint>
#include <memory>
#include <string>

namespace nav_msgs
{
namespace msg
{

struct GridCells
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "nav_msgs/msg/GridCells";
  using SharedPtr = std::shared_ptr<GridCells>;
  using ConstSharedPtr = std::shared_ptr<const GridCells>;
  using UniquePtr = std::unique_ptr<GridCells>;
  using _header_type = ::std_msgs::msg::Header;
  ::std_msgs::msg::Header header;
  using _cell_width_type = float;
  float cell_width = 0.0f;
  using _cell_height_type = float;
  float cell_height = 0.0f;
  using _cells_type = std::vector<::geometry_msgs::msg::Point>;
  std::vector<::geometry_msgs::msg::Point> cells;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(header);
    ar(cell_width);
    ar(cell_height);
    ar(cells);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(header);
    ar(cell_width);
    ar(cell_height);
    ar(cells);
  }

  bool operator==(const GridCells & other) const
  {
    return header == other.header &&
      cell_width == other.cell_width &&
      cell_height == other.cell_height &&
      cells == other.cells;
  }
  bool operator!=(const GridCells & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace nav_msgs

#endif  // NAV_MSGS_MSG_GRIDCELLS_HPP_
