#ifndef AMENT_INDEX_CPP__GET_PACKAGE_SHARE_DIRECTORY_HPP_
#define AMENT_INDEX_CPP__GET_PACKAGE_SHARE_DIRECTORY_HPP_

#include <string>

#include "ament_index_cpp/get_package_share_path.hpp"

namespace ament_index_cpp
{

inline std::string get_package_share_directory(const std::string & package_name)
{
  return detail::package_share_path(package_name).string();
}

}  // namespace ament_index_cpp

#endif  // AMENT_INDEX_CPP__GET_PACKAGE_SHARE_DIRECTORY_HPP_
