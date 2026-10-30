
#include <cstdio>
#include <string>
#include <vector>
#include "ros2_bridge/type_registry.hpp"
#include "ros2_bridge/yaml_assign.hpp"

int main(int argc, char ** argv)
{
  namespace yaml_assign = ros2_bridge::yaml_assign;
  for (int i = 1; i < argc; ++i) {
    std::printf("try %d: %s\n", i, argv[i]);
    std::fflush(stdout);
    std::vector<ros2_bridge::FieldAssign> fields;
    std::string error;
    const bool ok = yaml_assign::parseDocument(argv[i], fields, error);
    std::string rendered;
    for (const auto & f : fields) { if (!rendered.empty()) rendered += " "; rendered += f.path + "=" + f.value; }
    std::printf("  -> %s '%s' %s\n", ok ? "ok" : "rejected", rendered.c_str(), error.c_str());
    std::fflush(stdout);
  }
  return 0;
}
