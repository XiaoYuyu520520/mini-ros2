#ifndef AMENT_INDEX_CPP__GET_PACKAGE_SHARE_PATH_HPP_
#define AMENT_INDEX_CPP__GET_PACKAGE_SHARE_PATH_HPP_

#include <cstdlib>
#include <filesystem>
#include <string>

namespace ament_index_cpp
{

namespace detail
{

// Where a package's share directory is on this port.
//
// Upstream asks the ament index that `colcon build` writes. There is no such
// index here: the packages are the extracted sources and `ros2_shim` builds
// them where they stand, so `behavior_trees/`, `params/` and `config/` sit at
// the package root rather than under an install prefix. Three answers, in
// order, so an installed layout still wins over the checkout:
//
//   1. $NAV2_SHIM_PREFIX/share/<package>        - a real prefix, if one is set up
//   2. $NAV2_SHIM_SRC/<package>                 - the extracted navigation2 tree
//   3. $NAV2_SHIM_SRC/../third_party/<package>  - vendored packages beside it
//
// and the first answer again if none of them exists, so that the failure names
// the conventional location instead of an empty string. `NAV2_SHIM_SRC` is what
// the launcher scripts export (see bridge/windows/bridge_env.bat); a package
// that is not there is a build-time mistake, not something to paper over.
inline std::filesystem::path package_share_path(const std::string & package_name)
{
  std::error_code error;

  const char * prefix = std::getenv("NAV2_SHIM_PREFIX");
  const std::filesystem::path installed =
    std::filesystem::path(prefix ? prefix : ".") / "share" / package_name;
  if (std::filesystem::exists(installed, error)) {
    return installed;
  }

  const char * src = std::getenv("NAV2_SHIM_SRC");
  if (src && *src) {
    const std::filesystem::path source_root(src);
    const std::filesystem::path extracted = source_root / package_name;
    if (std::filesystem::exists(extracted, error)) {
      return extracted;
    }
    const std::filesystem::path vendored =
      source_root / ".." / "third_party" / package_name;
    if (std::filesystem::exists(vendored, error)) {
      return vendored;
    }
  }

  return installed;
}

}  // namespace detail

inline std::filesystem::path get_package_share_path(const std::string & package_name)
{
  return detail::package_share_path(package_name);
}

}  // namespace ament_index_cpp

#endif  // AMENT_INDEX_CPP__GET_PACKAGE_SHARE_PATH_HPP_
