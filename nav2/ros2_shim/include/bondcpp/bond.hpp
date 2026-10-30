#ifndef BONDCPP__BOND_HPP_
#define BONDCPP__BOND_HPP_

#include <chrono>
#include <memory>
#include <string>

#include "bond/msg/constants.hpp"

namespace bond
{

// Lightweight no-op bond used by the shared-memory shim. The lifecycle manager
// heartbeat is unnecessary when nodes live in the same address space / machine.
class Bond
{
public:
  template<class NodeT>
  Bond(const std::string &, const std::string &, std::shared_ptr<NodeT>)
  {
  }

  Bond(const std::string &, const std::string &, void * = nullptr)
  {
  }

  ~Bond() = default;

  void setHeartbeatPeriod(double) {}
  void setHeartbeatTimeout(double) {}
  void setConnectTimeout(double) {}
  void start() {}
  void breakBond() {}
  bool isBroken() {return false;}
  bool waitUntilFormed(std::chrono::milliseconds) {return true;}
  bool waitUntilFormed() {return true;}

  template<class DurationT>
  bool waitUntilFormed(const DurationT &) {return true;}

  static const char * getDefaultBondTopic() {return msg::Constants::HEARTBEAT_TOPIC;}
};

}  // namespace bond

#endif  // BONDCPP__BOND_HPP_
