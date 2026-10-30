#ifndef BOND__MSG__CONSTANTS_HPP_
#define BOND__MSG__CONSTANTS_HPP_

#include <string>

namespace bond
{
namespace msg
{

struct Constants
{
  static constexpr const char * DISABLE_HEARTBEAT_TIMEOUT_PARAM =
    "/disable_heartbeat_timeout";
  static constexpr const char * HEARTBEAT_TOPIC = "/bond";
  static constexpr const char * HEARTBEAT_TIMEOUT_PARAM = "/heartbeat_timeout";
};

}  // namespace msg
}  // namespace bond

#endif  // BOND__MSG__CONSTANTS_HPP_
