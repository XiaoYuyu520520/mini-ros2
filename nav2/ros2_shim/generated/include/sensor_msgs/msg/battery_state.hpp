#ifndef SENSOR_MSGS_MSG_BATTERYSTATE_HPP_
#define SENSOR_MSGS_MSG_BATTERYSTATE_HPP_

#include "std_msgs/msg/header.hpp"
#include <vector>

#include <cstdint>
#include <memory>
#include <string>

namespace sensor_msgs
{
namespace msg
{

struct BatteryState
{
  using ros2_message_tag = void;
  static constexpr const char * ros2_message_type = "sensor_msgs/msg/BatteryState";
  using SharedPtr = std::shared_ptr<BatteryState>;
  using ConstSharedPtr = std::shared_ptr<const BatteryState>;
  using UniquePtr = std::unique_ptr<BatteryState>;
  static constexpr uint8_t POWER_SUPPLY_STATUS_UNKNOWN = 0;
  static constexpr uint8_t POWER_SUPPLY_STATUS_CHARGING = 1;
  static constexpr uint8_t POWER_SUPPLY_STATUS_DISCHARGING = 2;
  static constexpr uint8_t POWER_SUPPLY_STATUS_NOT_CHARGING = 3;
  static constexpr uint8_t POWER_SUPPLY_STATUS_FULL = 4;
  static constexpr uint8_t POWER_SUPPLY_HEALTH_UNKNOWN = 0;
  static constexpr uint8_t POWER_SUPPLY_HEALTH_GOOD = 1;
  static constexpr uint8_t POWER_SUPPLY_HEALTH_OVERHEAT = 2;
  static constexpr uint8_t POWER_SUPPLY_HEALTH_DEAD = 3;
  static constexpr uint8_t POWER_SUPPLY_HEALTH_OVERVOLTAGE = 4;
  static constexpr uint8_t POWER_SUPPLY_HEALTH_UNSPEC_FAILURE = 5;
  static constexpr uint8_t POWER_SUPPLY_HEALTH_COLD = 6;
  static constexpr uint8_t POWER_SUPPLY_HEALTH_WATCHDOG_TIMER_EXPIRE = 7;
  static constexpr uint8_t POWER_SUPPLY_HEALTH_SAFETY_TIMER_EXPIRE = 8;
  static constexpr uint8_t POWER_SUPPLY_TECHNOLOGY_UNKNOWN = 0;
  static constexpr uint8_t POWER_SUPPLY_TECHNOLOGY_NIMH = 1;
  static constexpr uint8_t POWER_SUPPLY_TECHNOLOGY_LION = 2;
  static constexpr uint8_t POWER_SUPPLY_TECHNOLOGY_LIPO = 3;
  static constexpr uint8_t POWER_SUPPLY_TECHNOLOGY_LIFE = 4;
  static constexpr uint8_t POWER_SUPPLY_TECHNOLOGY_NICD = 5;
  static constexpr uint8_t POWER_SUPPLY_TECHNOLOGY_LIMN = 6;
  static constexpr uint8_t POWER_SUPPLY_TECHNOLOGY_TERNARY = 7;
  static constexpr uint8_t POWER_SUPPLY_TECHNOLOGY_VRLA = 8;

  using _header_type = ::std_msgs::msg::Header;
  ::std_msgs::msg::Header header;
  using _voltage_type = float;
  float voltage = 0.0f;
  using _temperature_type = float;
  float temperature = 0.0f;
  using _current_type = float;
  float current = 0.0f;
  using _charge_type = float;
  float charge = 0.0f;
  using _capacity_type = float;
  float capacity = 0.0f;
  using _design_capacity_type = float;
  float design_capacity = 0.0f;
  using _percentage_type = float;
  float percentage = 0.0f;
  using _power_supply_status_type = uint8_t;
  uint8_t power_supply_status = 0;
  using _power_supply_health_type = uint8_t;
  uint8_t power_supply_health = 0;
  using _power_supply_technology_type = uint8_t;
  uint8_t power_supply_technology = 0;
  using _present_type = bool;
  bool present = false;
  using _cell_voltage_type = std::vector<float>;
  std::vector<float> cell_voltage;
  using _cell_temperature_type = std::vector<float>;
  std::vector<float> cell_temperature;
  using _location_type = std::string;
  std::string location;
  using _serial_number_type = std::string;
  std::string serial_number;

  template <class Archive>
  void visit(Archive & ar)
  {
    ar(header);
    ar(voltage);
    ar(temperature);
    ar(current);
    ar(charge);
    ar(capacity);
    ar(design_capacity);
    ar(percentage);
    ar(power_supply_status);
    ar(power_supply_health);
    ar(power_supply_technology);
    ar(present);
    ar(cell_voltage);
    ar(cell_temperature);
    ar(location);
    ar(serial_number);
  }

  template <class Archive>
  void visit(Archive & ar) const
  {
    ar(header);
    ar(voltage);
    ar(temperature);
    ar(current);
    ar(charge);
    ar(capacity);
    ar(design_capacity);
    ar(percentage);
    ar(power_supply_status);
    ar(power_supply_health);
    ar(power_supply_technology);
    ar(present);
    ar(cell_voltage);
    ar(cell_temperature);
    ar(location);
    ar(serial_number);
  }

  bool operator==(const BatteryState & other) const
  {
    return header == other.header &&
      voltage == other.voltage &&
      temperature == other.temperature &&
      current == other.current &&
      charge == other.charge &&
      capacity == other.capacity &&
      design_capacity == other.design_capacity &&
      percentage == other.percentage &&
      power_supply_status == other.power_supply_status &&
      power_supply_health == other.power_supply_health &&
      power_supply_technology == other.power_supply_technology &&
      present == other.present &&
      cell_voltage == other.cell_voltage &&
      cell_temperature == other.cell_temperature &&
      location == other.location &&
      serial_number == other.serial_number;
  }
  bool operator!=(const BatteryState & other) const {return !(*this == other);}
};

}  // namespace msg
}  // namespace sensor_msgs

#endif  // SENSOR_MSGS_MSG_BATTERYSTATE_HPP_
