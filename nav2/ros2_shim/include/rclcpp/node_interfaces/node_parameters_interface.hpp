// ---------------------------------------------------------------------------
// rclcpp/node_interfaces/node_parameters_interface.hpp
//
// A forwarding header, and only that. Upstream rclcpp splits the node
// interfaces across one header per interface inside `rclcpp/node_interfaces/`;
// this shim declares all of them in the single umbrella
// `rclcpp/node_interfaces.hpp`, and most nav2 packages include the umbrella.
//
// `nav2_route` is the exception. `graph_loader.hpp` and `graph_saver.hpp` reach
// for the per-interface path, and in this release that include is all they use
// it for - neither file names `NodeParametersInterface`, `has_parameter` nor any
// other member of it. So the port only has to satisfy the include, and the class
// it would have found is already declared in the umbrella.
//
// Keeping it a forward rather than a second declaration matters: a copy of the
// class here would be a second definition of an interface the node hands out,
// and the two would drift.
// ---------------------------------------------------------------------------

#ifndef RCLCPP__NODE_INTERFACES__NODE_PARAMETERS_INTERFACE_HPP_
#define RCLCPP__NODE_INTERFACES__NODE_PARAMETERS_INTERFACE_HPP_

#include "rclcpp/node_interfaces.hpp"

#endif  // RCLCPP__NODE_INTERFACES__NODE_PARAMETERS_INTERFACE_HPP_
