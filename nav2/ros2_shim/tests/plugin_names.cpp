// A plugin is looked up by the name nav2 writes in its YAML, which is the name
// the class was *exported with* - and that is not always the name the compiler
// knows the type by. `nav2_smac_planner::SmacPlannerHybrid` is an alias for
// `SmacPlannerHybridT<NodeHybrid>`, so a registry keyed on `typeid` refuses a
// plugin that is sitting right there in the binary, with the one error that
// names the plugin as if it did not exist. What is checked here:
//
//   1. an exported alias resolves under the alias name, template arguments and
//      all,
//   2. the same plugin still resolves under its demangled type name, so nothing
//      that only ever matched that way stops matching,
//   3. a class exported from inside its own namespace resolves from either
//      spelling, `demo::Plain` or `Plain`,
//   4. a class exported with its template brackets written out - nav2_behaviors
//      exports `DriveOnHeading<>` but its plugin XML calls it
//      `nav2_behaviors::DriveOnHeading` - resolves under the bracket-free name,
//   5. two packages may export classes with the same short name
//      (`dwb_critics::PathAlignCritic` and `mppi::critics::PathAlignCritic`
//      really do) and each qualified name still resolves to its own class,
//      rather than to whichever was exported last.
//   6. the one class nav2's plugin XML renames is reachable under both the name
//      it is exported with and the name the XML gives it.
//   7. an unknown name still fails, and the message says which name.
//
// It runs without the shared-memory graph and without loading a library: the
// shim's plugins are linked in, so a registrar is a static initialiser.
#include "pluginlib/class_loader.hpp"
#include "pluginlib/class_list_macros.hpp"

#include <cstdio>
#include <memory>
#include <string>

namespace demo
{

class Base
{
public:
  virtual ~Base() = default;
  virtual int id() const {return 0;}
};

template<typename NodeT>
class PlannerT : public Base
{
public:
  int id() const override {return 1;}
};

/// Stands in for nav2_smac_planner::NodeHybrid.
struct NodeHybrid {};

/// The alias nav2 names in its YAML; the type is PlannerT<NodeHybrid>.
using Planner = PlannerT<NodeHybrid>;

class Plain : public Base
{
public:
  int id() const override {return 2;}
};

/// Has no counterpart in another namespace, so both spellings of it are
/// unambiguous.
class Solo : public Base
{
public:
  int id() const override {return 5;}
};

/// Stands in for nav2_behaviors::DriveOnHeading, whose export writes the
/// default template argument out.
template<typename ParamT = void>
class Steered : public Base
{
public:
  int id() const override {return 3;}
};

namespace other
{

/// Shares its short name with `demo::Plain`, the way dwb_critics and
/// mppi::critics share `PathAlignCritic`.
class Plain : public Base
{
public:
  int id() const override {return 4;}
};

}  // namespace other

}  // namespace demo

namespace mppi
{

/// Exported under its C++ type name; `trajectory_validators.xml` gives it the
/// loadable name `mppi::DefaultOptimalTrajectoryValidator`, and that is the name
/// the controller's default parameter asks for.
class OptimalTrajectoryValidator : public demo::Base
{
public:
  int id() const override {return 6;}
};

}  // namespace mppi

PLUGINLIB_EXPORT_CLASS(demo::Planner, demo::Base)
PLUGINLIB_EXPORT_CLASS(demo::Solo, demo::Base)
PLUGINLIB_EXPORT_CLASS(demo::Plain, demo::Base)
PLUGINLIB_EXPORT_CLASS(demo::Steered<>, demo::Base)
PLUGINLIB_EXPORT_CLASS(demo::other::Plain, demo::Base)
PLUGINLIB_EXPORT_CLASS(mppi::OptimalTrajectoryValidator, demo::Base)

namespace
{

int failures = 0;

void expect(bool condition, const std::string & what)
{
  if (!condition) {
    std::printf("  FAIL %s\n", what.c_str());
    ++failures;
  }
}

void section(const char * name)
{
  std::printf("%s\n", name);
}

}  // namespace

int main()
{
  pluginlib::ClassLoader<demo::Base> loader("demo", "demo::Base");

  section("an exported alias resolves under the alias name");
  auto planner = loader.createSharedInstance("demo::Planner");
  expect(planner != nullptr, "`demo::Planner` creates");
  expect(planner->id() == 1, "and it is the class that was exported");

  section("and still under its demangled type name");
  auto by_type = loader.createSharedInstance("demo::PlannerT<demo::NodeHybrid>");
  expect(by_type != nullptr, "`demo::PlannerT<demo::NodeHybrid>` creates");
  expect(by_type->id() == 1, "and it is the same class");

  section("a non-template class resolves from either spelling");
  expect(loader.createSharedInstance("demo::Solo")->id() == 5, "qualified");
  expect(loader.createSharedInstance("Solo")->id() == 5, "unqualified");

  section("a class exported as `Steered<>` resolves as `demo::Steered`");
  expect(loader.isClassAvailable("demo::Steered"), "the bracket-free name is declared");
  expect(
    loader.createSharedInstance("demo::Steered")->id() == 3,
    "and it creates the class that was exported");
  expect(
    loader.createUniqueInstance("demo::Steered") != nullptr,
    "the unique-instance path finds it too");
  expect(loader.createSharedInstance("demo::Steered<>")->id() == 3, "as does the written-out one");

  section("a short name shared by two packages resolves to the right one each time");
  expect(
    loader.createSharedInstance("demo::Plain")->id() == 2,
    "`demo::Plain` is demo::Plain, even though demo::other::Plain was exported later");
  expect(
    loader.createSharedInstance("demo::other::Plain")->id() == 4,
    "`demo::other::Plain` is the other one");
  expect(
    loader.isClassAvailable("demo::other::Plain"),
    "and both qualified names are declared");
  const int bare_id = loader.createSharedInstance("Plain")->id();
  expect(
    bare_id == 2 || bare_id == 4,
    "the bare name still resolves - to one of them, whichever was exported last");

  section("a class renamed by its plugin XML is reachable under both names");
  expect(
    loader.createSharedInstance("mppi::OptimalTrajectoryValidator")->id() == 6,
    "the exported type name resolves");
  expect(
    loader.createSharedInstance("mppi::DefaultOptimalTrajectoryValidator")->id() == 6,
    "and the name trajectory_validators.xml declares resolves to the same class");

  section("the names are answerable, and an unknown one is not");
  expect(loader.isClassAvailable("demo::Planner"), "the alias is declared");
  expect(loader.isClassAvailable("Planner"), "the unqualified alias too");
  expect(!loader.isClassAvailable("demo::Missing"), "an unexported name is not");

  bool threw = false;
  std::string message;
  try {
    loader.createSharedInstance("demo::Missing");
  } catch (const std::exception & error) {
    threw = true;
    message = error.what();
  }
  expect(threw, "creating an unexported name throws");
  expect(
    message.find("demo::Missing") != std::string::npos,
    "and the message names what was looked up");

  if (failures != 0) {
    std::printf("\n%d check(s) failed\n", failures);
    return 1;
  }
  std::printf("\nall checks passed\n");
  return 0;
}
