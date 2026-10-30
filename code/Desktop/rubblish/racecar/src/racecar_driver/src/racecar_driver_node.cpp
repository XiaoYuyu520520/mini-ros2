//
// racecar_driver_node.cpp
//

#include "rclcpp/rclcpp.hpp"
#include "geometry_msgs/msg/twist.hpp"
#include "../include/racecar_driver.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <string>
#include <thread>

class RacecarDriver : public rclcpp::Node
{
public:
RacecarDriver()
: Node("racecar_driver")
{
this->declare_parameter<std::string>("serial_port", "/dev/car");
this->declare_parameter<int>("baud_rate", 38400);

// 速度/安全限制参数（可在 config/driver.yaml 中配置）
this->declare_parameter<double>("max_linear_vel", 2.0);   // 最大线速度(m/s)
this->declare_parameter<double>("min_linear_vel", -2.0);  // 最小线速度(m/s)
this->declare_parameter<double>("cmd_vel_timeout", 0.5);  // 指令超时(s)
this->declare_parameter<int>("min_motor_pwm", 1000);      // 电机PWM下限(us)
this->declare_parameter<int>("max_motor_pwm", 2000);      // 电机PWM上限(us)
this->declare_parameter<int>("min_servo_pwm", 500);       // 舵机PWM下限(us)
this->declare_parameter<int>("max_servo_pwm", 2500);      // 舵机PWM上限(us)

// Ackermann 运动学 + 转向标定参数
this->declare_parameter<double>("wheelbase", 0.305);             // 前后轴距(m)
this->declare_parameter<double>("steering_center_pwm", 1500.0);  // 前轮居中舵机PWM(us)
this->declare_parameter<double>("steering_pwm_per_rad", 800.0);  // 前轮转角(rad)->PWM(us/rad)
this->declare_parameter<double>("max_steering_angle_rad", 0.5);  // 前轮最大转角(rad)
this->declare_parameter<double>("min_steering_speed", 0.05);     // 低于该速度不按 w/v 计算(m/s)

// 控制权仲裁参数
this->declare_parameter<std::string>("control_mode", "priority"); // auto | manual | priority
this->declare_parameter<double>("teleop_timeout", 0.3);           // 遥控失联判定(s)

std::string serial_port;
int baud_rate;

this->get_parameter("serial_port", serial_port);
this->get_parameter("baud_rate", baud_rate);
this->get_parameter("max_linear_vel", max_linear_vel_);
this->get_parameter("min_linear_vel", min_linear_vel_);
this->get_parameter("cmd_vel_timeout", cmd_vel_timeout_);
this->get_parameter("min_motor_pwm", min_motor_pwm_);
this->get_parameter("max_motor_pwm", max_motor_pwm_);
this->get_parameter("min_servo_pwm", min_servo_pwm_);
this->get_parameter("max_servo_pwm", max_servo_pwm_);
this->get_parameter("wheelbase", wheelbase_);
this->get_parameter("steering_center_pwm", steering_center_pwm_);
this->get_parameter("steering_pwm_per_rad", steering_pwm_per_rad_);
this->get_parameter("max_steering_angle_rad", max_steering_angle_rad_);
this->get_parameter("min_steering_speed", min_steering_speed_);
this->get_parameter("control_mode", control_mode_);
this->get_parameter("teleop_timeout", teleop_timeout_);

// Convert std::string to char*
char serial_port_char[serial_port.size() + 1];
std::strcpy(serial_port_char, serial_port.c_str());

art_racecar_init(baud_rate, serial_port_char);

subscription_ = this->create_subscription<geometry_msgs::msg::Twist>(
// "/cmd_vel", 1, std::bind(&RacecarDriver::TwistCallback, this, std::placeholders::_1));
"/car_cmd_vel", 1, std::bind(&RacecarDriver::TwistCallback, this, std::placeholders::_1));

teleop_subscription_ = this->create_subscription<geometry_msgs::msg::Twist>(
    "/teleop_cmd_vel", 1, std::bind(&RacecarDriver::teleop_TwistCallback, this, std::placeholders::_1));

// 看门狗：一旦上层控制节点崩溃/失联，主动输出停车指令，避免底盘失控
last_cmd_time_ = std::chrono::steady_clock::now();
last_teleop_time_ = std::chrono::steady_clock::now();
watchdog_timer_ = this->create_wall_timer(
    std::chrono::milliseconds(100),
    std::bind(&RacecarDriver::watchdog_callback, this));

RCLCPP_INFO(
    this->get_logger(),
    "[底盘] 启动完成，control_mode=%s，wheelbase=%.3f m",
    control_mode_.c_str(), wheelbase_);
}

~RacecarDriver() override
{
    // driver 自身退出（Ctrl+C / 正常结束）时主动回中位，
    // 否则 MCU 可能一直保持最后一次收到的 PWM。
    stop_car();
}

private:
// 向底盘重复发送中位停车，(1500,1500) 对应 PWM 中位
void stop_car()
{
    try {
        send_cmd(1500, 1500);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        send_cmd(1500, 1500);
    } catch (...) {
        // 析构中不允许异常逃逸
    }
}

// 将 double 限幅后安全转换为 uint16_t，避免负值/超大值溢出
static uint16_t clamp_pwm(double value, int lo, int hi)
{
  if (value < static_cast<double>(lo)) {
    return static_cast<uint16_t>(lo);
  }
  if (value > static_cast<double>(hi)) {
    return static_cast<uint16_t>(hi);
  }
  return static_cast<uint16_t>(value);
}

// 遥控是否仍在持续发布
bool teleop_alive(const std::chrono::steady_clock::time_point& now) const
{
    return std::chrono::duration<double>(now - last_teleop_time_).count() < teleop_timeout_;
}

void TwistCallback(const geometry_msgs::msg::Twist::SharedPtr twist)
{
auto now = std::chrono::steady_clock::now();

// 仲裁：manual 模式忽略自动；priority 模式下遥控活跃时自动让位
if (control_mode_ == "manual") {
    return;
}
if (control_mode_ == "priority" &&
    active_source_ == ControlSource::kManual && teleop_alive(now)) {
    return;
}

// 链条：/car_cmd_vel 使用 SI 单位，v(m/s) -> 电机 PWM = v * 100 + 1500
double v = twist->linear.x;
if (v > max_linear_vel_ || v < min_linear_vel_) {
  double limited = std::min(std::max(v, min_linear_vel_), max_linear_vel_);
  RCLCPP_ERROR(
      this->get_logger(),
      "\033[31m[底盘警告] 线速度 %.2f m/s 超出允许范围 [%.2f, %.2f] m/s，已限制为 %.2f m/s\033[0m",
      v, min_linear_vel_, max_linear_vel_, limited);
  v = limited;
}

double w = twist->angular.z;  // rad/s
double raw_linear_pwm = v * 100.0 + 1500.0;

// Ackermann 运动学：前轮转角 delta = atan(L * w / v)
// 同一个曲率弯道（w/v 相同）在任何速度下都应得到相同前轮转角，
// 不能直接用 w * 常数（否则降速时转向不足）。
double steering_angle = 0.0;
if (std::abs(v) >= min_steering_speed_) {
    steering_angle = std::atan(wheelbase_ * w / v);
} else if (std::abs(w) > 1e-3) {
    // 极低速/静止：退化为最大转角（按转向符号）
    steering_angle = std::copysign(max_steering_angle_rad_, w);
}
steering_angle = std::clamp(
    steering_angle, -max_steering_angle_rad_, max_steering_angle_rad_);

double raw_angle_pwm =
    steering_center_pwm_ - steering_angle * steering_pwm_per_rad_;

uint16_t motor_pwm = clamp_pwm(raw_linear_pwm, min_motor_pwm_, max_motor_pwm_);
uint16_t servo_pwm = clamp_pwm(raw_angle_pwm, min_servo_pwm_, max_servo_pwm_);

active_source_ = ControlSource::kAuto;
last_cmd_time_ = now;
cmd_received_ = true;
timed_out_ = false;

linear_x = motor_pwm;
angle = servo_pwm;
RCLCPP_INFO(this->get_logger(),
            "自动 线速度: %.2f m/s (PWM %u)  角速度: %.2f rad/s  转角: %.1f deg (PWM %u)",
            v, static_cast<unsigned>(motor_pwm), w,
            steering_angle * 180.0 / M_PI, static_cast<unsigned>(servo_pwm));

send_cmd(motor_pwm, servo_pwm);
}


void teleop_TwistCallback(const geometry_msgs::msg::Twist::SharedPtr twist)
{
    auto now = std::chrono::steady_clock::now();

    // /teleop_cmd_vel 保留原始 PWM 约定：linear.x = 电机 PWM, angular.z = 舵机角度(度)
    double raw_angle = 2500.0 - twist->angular.z * 2000.0 / 180.0;
    uint16_t motor_pwm = clamp_pwm(twist->linear.x, min_motor_pwm_, max_motor_pwm_);
    uint16_t servo_pwm = clamp_pwm(raw_angle, min_servo_pwm_, max_servo_pwm_);

    bool neutral = (motor_pwm == 1500 && servo_pwm == 1500);

    // 仲裁：auto 模式忽略遥控
    if (control_mode_ == "auto") {
        return;
    }
    // 遥控空闲（持续发中位）且当前并未由遥控接管时，不抢控制权，
    // 否则键盘遥控一启动就会不断发中位，把 Nav2 巡航打断。
    if (neutral && active_source_ != ControlSource::kManual) {
        return;
    }

    active_source_ = ControlSource::kManual;
    last_teleop_time_ = now;
    last_cmd_time_ = now;
    cmd_received_ = true;
    timed_out_ = false;

    angle = servo_pwm;
    linear_x = motor_pwm;

    RCLCPP_INFO(this->get_logger(), "遥控 线速度PWM: %u    角速度PWM: %u",
                static_cast<unsigned>(motor_pwm), static_cast<unsigned>(servo_pwm));

    send_cmd(motor_pwm, servo_pwm);
}

void watchdog_callback()
{
    auto now = std::chrono::steady_clock::now();

    // priority 模式下遥控长时间无消息，交还自动驾驶
    if (active_source_ == ControlSource::kManual && !teleop_alive(now)) {
        active_source_ = ControlSource::kAuto;
        RCLCPP_WARN(this->get_logger(), "[底盘仲裁] 遥控失联，交还自动驾驶");
    }

    if (!cmd_received_) {
        return;
    }
    double elapsed = std::chrono::duration<double>(now - last_cmd_time_).count();
    if (elapsed > cmd_vel_timeout_) {
        if (!timed_out_) {
            timed_out_ = true;
            send_cmd(1500, 1500);
            RCLCPP_WARN(
                this->get_logger(),
                "[底盘看门狗] %.2f 秒未收到有效控制指令，已输出停车指令 (1500,1500)",
                cmd_vel_timeout_);
        }
    } else {
        timed_out_ = false;
    }
}


rclcpp::Subscription<geometry_msgs::msg::Twist>::SharedPtr subscription_;
rclcpp::Subscription<geometry_msgs::msg::Twist>::SharedPtr teleop_subscription_;
rclcpp::TimerBase::SharedPtr watchdog_timer_;
    double linear_x;            // 全局变量
    double angle;               // 全局变量
    double max_linear_vel_ = 2.0;
    double min_linear_vel_ = -2.0;
    double cmd_vel_timeout_ = 0.5;
    int min_motor_pwm_ = 1000;
    int max_motor_pwm_ = 2000;
    int min_servo_pwm_ = 500;
    int max_servo_pwm_ = 2500;

    // Ackermann / 转向标定
    double wheelbase_ = 0.305;
    double steering_center_pwm_ = 1500.0;
    double steering_pwm_per_rad_ = 800.0;
    double max_steering_angle_rad_ = 0.5;
    double min_steering_speed_ = 0.05;

    // 控制权仲裁
    enum class ControlSource { kNone, kAuto, kManual };
    std::string control_mode_ = "priority";
    double teleop_timeout_ = 0.3;
    ControlSource active_source_ = ControlSource::kNone;

    std::chrono::steady_clock::time_point last_cmd_time_;
    std::chrono::steady_clock::time_point last_teleop_time_;
    bool cmd_received_ = false;
    bool timed_out_ = false;
};

int main(int argc, char** argv)
{
rclcpp::init(argc, argv);
auto node = std::make_shared<RacecarDriver>();
rclcpp::spin(node);
// 显式复位节点，确保在 shutdown 前先走析构停车
node.reset();
rclcpp::shutdown();
return 0;
}
