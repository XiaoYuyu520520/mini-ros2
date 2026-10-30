#ifndef DIAGNOSTIC_UPDATER__DIAGNOSTIC_UPDATER_HPP_
#define DIAGNOSTIC_UPDATER__DIAGNOSTIC_UPDATER_HPP_

#include <string>
#include <vector>

#include "diagnostic_msgs/msg/diagnostic_status.hpp"
#include "diagnostic_msgs/msg/key_value.hpp"

namespace diagnostic_updater
{

class DiagnosticStatusWrapper : public diagnostic_msgs::msg::DiagnosticStatus
{
public:
  void summary(int level, const std::string & msg)
  {
    this->level = static_cast<int8_t>(level);
    this->message = msg;
  }

  void summary(const std::string & msg) {this->message = msg;}

  void add(const std::string & key, const std::string & value)
  {
    diagnostic_msgs::msg::KeyValue kv;
    kv.key = key;
    kv.value = value;
    values.push_back(kv);
  }

  template<class T>
  void add(const std::string & key, const T & value)
  {
    add(key, std::to_string(value));
  }

  void mergeSummary(int level, const std::string & msg)
  {
    summary(level, msg);
  }

  void mergeSummary(const std::string & msg) {summary(msg);}
};

class Updater
{
public:
  template<class NodeT>
  explicit Updater(const NodeT *)
  {
  }

  void setHardwareID(const std::string &) {}

  template<class T>
  void add(
    const std::string &, T *, void (T::*)(DiagnosticStatusWrapper &))
  {
  }

  void force_update() {}

  void update() {}
};

using DiagnosticUpdater = Updater;

}  // namespace diagnostic_updater

#endif  // DIAGNOSTIC_UPDATER__DIAGNOSTIC_UPDATER_HPP_
