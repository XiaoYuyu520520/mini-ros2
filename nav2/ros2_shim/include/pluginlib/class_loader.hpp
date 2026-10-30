#ifndef PLUGINLIB__CLASS_LOADER_HPP_
#define PLUGINLIB__CLASS_LOADER_HPP_

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <typeinfo>
#include <vector>

#include "pluginlib/exceptions.hpp"

#if defined(__GNUC__)
#include <cxxabi.h>
#include <cstdlib>
#endif

namespace pluginlib
{
namespace detail
{

inline std::string demangle(const char * name)
{
#if defined(__GNUC__)
  int status = 0;
  char * result = abi::__cxa_demangle(name, nullptr, nullptr, &status);
  if (status == 0 && result) {
    std::string out(result);
    std::free(result);
    return out;
  }
#endif
  return name;
}

template<class T>
inline std::string type_name()
{
  return demangle(typeid(T).name());
}

/// The part of a plugin name after its last namespace separator.
///
/// Template arguments are skipped, because `::` occurs inside them: the last
/// `::` in `ns::PlannerT<ns::Node>` belongs to `Node`, and splitting there would
/// register the plugin under `Node>`.
inline std::string unqualified(const std::string & full)
{
  size_t depth = 0;
  size_t cut = std::string::npos;
  for (size_t i = 0; i < full.size(); ++i) {
    const char c = full[i];
    if (c == '<') {
      ++depth;
    } else if (c == '>') {
      if (depth > 0) {
        --depth;
      }
    } else if (depth == 0 && c == ':' && i + 1 < full.size() && full[i + 1] == ':') {
      cut = i + 2;
      ++i;
    }
  }
  return cut == std::string::npos ? full : full.substr(cut);
}

using Factory = std::function<std::shared_ptr<void>()>;
using RawFactory = std::function<void *()>;

inline std::map<std::string, Factory> & registry()
{
  static std::map<std::string, Factory> instance;
  return instance;
}

inline std::map<std::string, RawFactory> & raw_registry()
{
  static std::map<std::string, RawFactory> instance;
  return instance;
}

/// The spelling with any template argument list dropped.
///
/// `PLUGINLIB_EXPORT_CLASS(nav2_behaviors::DriveOnHeading<>, ...)` stringifies
/// its first argument *as written*, so the key that leaves is
/// `DriveOnHeading<>`. The name a plugin is actually looked up by is the one in
/// its plugin XML, and nav2's `behavior_plugin.xml` gives that class
/// `name="nav2_behaviors::DriveOnHeading"` - the template brackets are in the
/// `type` attribute, not the name. Registering the bracket-free spelling as a
/// third key is what makes that lookup land, and it is the general rule rather
/// than a patch for one class: a plugin XML never spells a name with `<>`.
inline std::string without_template_arguments(const std::string & name)
{
  const auto bracket = name.find('<');
  return bracket == std::string::npos ? name : name.substr(0, bracket);
}

inline std::string normalize(const std::string & name)
{
  const auto slash = name.find('/');
  if (slash != std::string::npos) {
    return name.substr(slash + 1);
  }
  return unqualified(name);
}

/// The loadable name a plugin XML gives a class whose C++ type differs.
///
/// pluginlib in ROS reads each package's plugin XML, so a class has two names:
/// the `name` attribute, which is what a configuration file looks up, and the
/// `type` attribute, which is only what gets constructed. Nothing here reads the
/// XML - the shim links its plugins in rather than opening a library - so a
/// lookup name reaches the registry as written, and only matches when the
/// configuration spells the `type`. That holds for every nav2 class this port
/// uses except one, so the exception is written out rather than the rule being
/// generalised: `nav2_mppi_controller`'s `trajectory_validators.xml` calls
/// `mppi::OptimalTrajectoryValidator` by the name
/// `mppi::DefaultOptimalTrajectoryValidator`, and `optimizer.cpp` declares
/// `TrajectoryValidator.plugin` with that name as its default. Without the
/// mapping the controller fails to configure with
/// `no registered plugin matching 'mppi::DefaultOptimalTrajectoryValidator'`.
/// The other XML names that differ from their type are RViz panels, which are
/// not part of this port, `DriveOnHeading<>`, which the template-free spelling
/// already covers, and the constrained smoother, which is not ported.
inline std::string xml_name_alias(const std::string & name)
{
  if (name == "mppi::DefaultOptimalTrajectoryValidator") {
    return "mppi::OptimalTrajectoryValidator";
  }
  return name;
}

/// Every registry key a lookup should try, most specific first.
///
/// The namespace-qualified spelling has to come first, because a short name is
/// not unique across packages: `dwb_critics::PathAlignCritic` and
/// `mppi::critics::PathAlignCritic` are different classes, and a registry keyed
/// on `PathAlignCritic` alone lets whichever is exported second overwrite the
/// first. That is not hypothetical - `CriticManager::loadCritics()` builds its
/// lookup name with `getFullName()`, so MPPI asked for the qualified spelling,
/// got DWB's critic back, and then called `on_configure` through a vtable that
/// did not belong to the object. The unqualified form stays as the
/// final fallback because that is what a plugin XML `<class name="pkg/Class">`
/// lookup reduces to.
inline std::vector<std::string> lookup_keys(const std::string & name)
{
  std::vector<std::string> keys;
  const std::string spelled = without_template_arguments(xml_name_alias(name));
  if (spelled.find("::") != std::string::npos) {
    keys.push_back(spelled);
  }
  const std::string fallback = normalize(xml_name_alias(name));
  if (keys.empty() || keys.front() != fallback) {
    keys.push_back(fallback);
  }
  return keys;
}

/// The first key `lookup_keys` asks for that `map` actually has.
template<typename Map>
inline typename Map::const_iterator find_factory(const Map & map, const std::string & name)
{
  for (const auto & key : lookup_keys(name)) {
    const auto it = map.find(key);
    if (it != map.end()) {
      return it;
    }
  }
  return map.end();
}

/// Registers a plugin under the name it was exported with, and under its
/// demangled type.
///
/// `name` is the type as *written* at the export site, which is what upstream
/// keys on - `CLASS_LOADER_REGISTER_CLASS` stringifies its argument - and what a
/// plugin's XML and nav2's YAML name. The two differ whenever the written name
/// is an alias, and that is not a corner case: `nav2_smac_planner::SmacPlannerHybrid`
/// is `SmacPlannerHybridT<NodeHybrid>` to the compiler, so a registry keyed on
/// `typeid` alone answers `no registered plugin matching
/// "nav2_smac_planner::SmacPlannerHybrid"`. The demangled name stays as a second
/// key rather than being replaced, so a name that only ever matched that way
/// keeps matching - and the template-free spelling is a third, for exports that
/// do write their brackets out (`DriveOnHeading<>`).
template<class Derived, class Base>
inline void register_plugin(const std::string & name)
{
  const std::string spelled = unqualified(name);
  const std::string demangled = unqualified(type_name<Derived>());
  auto shared_factory = []() -> std::shared_ptr<void> {
      return std::static_pointer_cast<void>(std::make_shared<Derived>());
    };
  auto raw_factory = []() -> void * {
      return static_cast<void *>(static_cast<Base *>(new Derived()));
    };
  registry()[spelled] = shared_factory;
  raw_registry()[spelled] = raw_factory;
  const std::string bare = without_template_arguments(spelled);
  if (bare != spelled) {
    registry()[bare] = shared_factory;
    raw_registry()[bare] = raw_factory;
  }
  if (demangled != spelled) {
    registry()[demangled] = shared_factory;
    raw_registry()[demangled] = raw_factory;
  }
  // The namespace-qualified spellings as well, so that a lookup naming a package
  // gets the class from that package rather than one that merely shares its
  // short name. See `lookup_keys` for what goes wrong without them.
  for (const std::string & key : {without_template_arguments(name),
      without_template_arguments(demangled)})
  {
    if (key != spelled && key != bare && key != demangled) {
      registry()[key] = shared_factory;
      raw_registry()[key] = raw_factory;
    }
  }
  (void)type_name<Base>();
}

}  // namespace detail

class ClassLoaderBase
{
public:
  virtual ~ClassLoaderBase() = default;
};

template<class T>
using UniquePtr = std::unique_ptr<T>;

template<class Base>
class ClassLoader : public ClassLoaderBase
{
public:
  ClassLoader() = default;

  ClassLoader(const std::string & package, const std::string & base_class)
  : package_(package), base_class_(base_class)
  {
  }

  std::shared_ptr<Base> createSharedInstance(const std::string & lookup_name)
  {
    auto & reg = detail::registry();
    auto it = detail::find_factory(reg, lookup_name);
    if (it == reg.end()) {
      throw CreateClassException(
        "pluginlib: no registered plugin matching '" + lookup_name + "'");
    }
    return std::static_pointer_cast<Base>(it->second());
  }

  UniquePtr<Base> createUniqueInstance(const std::string & lookup_name)
  {
    auto & reg = detail::raw_registry();
    auto it = detail::find_factory(reg, lookup_name);
    if (it == reg.end()) {
      throw CreateClassException(
        "pluginlib: no registered plugin matching '" + lookup_name + "'");
    }
    return UniquePtr<Base>(static_cast<Base *>(it->second()));
  }

  Base * createUnmanagedInstance(const std::string & lookup_name)
  {
    auto & reg = detail::raw_registry();
    auto it = detail::find_factory(reg, lookup_name);
    if (it == reg.end()) {
      throw CreateClassException(
        "pluginlib: no registered plugin matching '" + lookup_name + "'");
    }
    return static_cast<Base *>(it->second());
  }

  bool isClassLoaded(const std::string &) const {return true;}

  bool isClassAvailable(const std::string & lookup_name) const
  {
    const auto & reg = detail::registry();
    return detail::find_factory(reg, lookup_name) != reg.end();
  }

  std::vector<std::string> getDeclaredClasses() const
  {
    std::vector<std::string> names;
    for (const auto & entry : detail::registry()) {
      names.push_back(entry.first);
    }
    return names;
  }

  const std::string & getClassType() const {return base_class_;}
  const std::string & getBaseClassType() const {return base_class_;}
  const std::string & getPackage() const {return package_;}

private:
  std::string package_;
  std::string base_class_;
};

}  // namespace pluginlib

// The type name is stringified, not taken from `typeid`: it is the name as
// written here, which is the name a plugin is looked up by. See
// `register_plugin` for why the difference matters.
#define PLUGINLIB_EXPORT_CLASS_IMPL(class_type, base_class, uid) \
  namespace { \
  struct PluginlibRegistrar_##uid \
  { \
    PluginlibRegistrar_##uid() \
    { \
      ::pluginlib::detail::register_plugin<class_type, base_class>(#class_type); \
    } \
  }; \
  static PluginlibRegistrar_##uid g_pluginlib_registrar_##uid; \
  }

// One level of indirection before the paste, so that `__LINE__` is expanded
// first. Passed straight to `PLUGINLIB_EXPORT_CLASS_IMPL`, whose `uid` is used
// with `##` and therefore taken unexpanded, it pastes as the literal
// `__LINE__`: every export in a translation unit would then declare the same
// registrar, and a file exporting two plugins would not compile.
#define PLUGINLIB_EXPORT_CLASS_IMPL_(class_type, base_class, uid) \
  PLUGINLIB_EXPORT_CLASS_IMPL(class_type, base_class, uid)

#define PLUGINLIB_EXPORT_CLASS(class_type, base_class) \
  PLUGINLIB_EXPORT_CLASS_IMPL_(class_type, base_class, __LINE__)

#endif  // PLUGINLIB__CLASS_LOADER_HPP_
