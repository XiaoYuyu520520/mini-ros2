#ifndef PLUGINLIB__CLASS_LIST_MACROS_HPP_
#define PLUGINLIB__CLASS_LIST_MACROS_HPP_

#include "pluginlib/class_loader.hpp"

// `PLUGINLIB_EXPORT_CLASS` and its `_IMPL_` indirection live in class_loader.hpp
// - the shim has no plugin description XML to load, so the registry and the
// macro that fills it belong together - and this header is the name upstream
// code includes them from.

#endif  // PLUGINLIB__CLASS_LIST_MACROS_HPP_
