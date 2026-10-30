cmake_minimum_required(VERSION 3.5)
project(racecar)

# Default to C++14
if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 14)
endif()

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Find dependencies
find_package(ament_cmake REQUIRED)
find_package(ament_cmake_python REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclpy REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(nav_msgs REQUIRED)
find_package(tf2 REQUIRED)
find_package(tf2_ros REQUIRED)
include_directories(${tf2_ros_INCLUDE_DIRS})
find_package(tf2_geometry_msgs REQUIRED)
find_package(message_filters REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(std_msgs REQUIRED)
find_package(visualization_msgs REQUIRED)
find_package(nav2_amcl REQUIRED)
find_package(action_msgs REQUIRED)

set(dependencies
  geometry_msgs
  sensor_msgs
  rclcpp
  std_msgs
  visualization_msgs
  tf2
  tf2_ros
  tf2_geometry_msgs
  nav_msgs
  message_filters
  ament_cmake
  nav2_amcl 
  action_msgs
)

# Include Cpp "include" directory
include_directories(include)

# Create Cpp executable
# ---------------------------------------------------------------------------
# 注意：以下旧控制/雷达节点仍按旧的 "PWM + 舵机角度" 约定向 /car_cmd_vel 发布，
# 而新版 racecar_driver 已把 /car_cmd_vel 定义为 SI 单位 (linear.x=m/s,
# angular.z=rad/s)。一旦误启动，1525 会被当成 1525 m/s、78 会被当成 78 rad/s，
# 直接变成极端油门/打死舵角。
#
# 这些节点当前未使用，先停止编译与安装，避免误启动。若要恢复，必须先把它们
# 迁移到 SI 单位，或改发 /teleop_cmd_vel（该话题才允许 PWM/角度约定）。
#
# 这些 .cpp 已经整体挪到 code/Desktop/rubblish/racecar/chassis_legacy/src/，
# 那里有 README 说明每个文件原来在哪；要恢复先按它搬回来。
# ---------------------------------------------------------------------------
# add_executable(car_controller_new src/car_controller_new.cpp)
# add_executable(laser_2d_new src/laser_2d_new.cpp)
# add_executable(my_car_control_new src/my_car_control_new.cpp)
# add_executable(scan_node src/scan_node.cpp)
# add_executable(car_control_all src/car_control_all.cpp)
# ament_target_dependencies(car_controller_new ${dependencies})
# ament_target_dependencies(laser_2d_new ${dependencies})
# ament_target_dependencies(my_car_control_new ${dependencies})
# ament_target_dependencies(scan_node ${dependencies})
# ament_target_dependencies(car_control_all ${dependencies})

# Install Cpp executables
# install(TARGETS
#  car_controller_new
#  laser_2d_new
#  my_car_control_new
#  scan_node
#  car_control_all
#  DESTINATION lib/${PROJECT_NAME}
# )

# Install Python modules
ament_python_install_package(${PROJECT_NAME})

# Install Python executables
#
# 每个脚本都要装：`racecar_ctl.py run <脚本>` 的 ros2 后端就是 `ros2 run racecar <脚本>`，
# 这里只装四个的话，另外十三个在车上根本没有可执行文件。航线 CSV 也一起装 —— 脚本按自己的
# __file__ 找航线，装上去的那份旁边没有 csv，就只能退回随包的 ai_test.csv。
# 名字里有空格的必须加引号，CMake 不然会把一个路径当成两个参数。
install(PROGRAMS
  scripts/ai_第一个点后回终点.py
  scripts/detct.py
  scripts/end_3_12.py
  scripts/get.py
  "scripts/go copy.py"
  "scripts/go copy_多点需要切换.py"
  scripts/go.py
  scripts/go_2c.py
  scripts/goal_l.py
  scripts/go一条路径.py
  scripts/一条路径+csv.py
  scripts/人工智能赛道初始版本代码.py
  scripts/会跳过几个点.py
  scripts/第二个点nav2打断，然后转圈圈.py
  scripts/终点不停.py
  scripts/跑一圈以内.py
  scripts/重复载入完成圈数.py
  DESTINATION lib/${PROJECT_NAME}
)

install(FILES
  scripts/ai_test.csv
  scripts/ai_test1.csv
  scripts/ai_test2.csv
  scripts/sim_room12.csv
  DESTINATION lib/${PROJECT_NAME}
)

install(DIRECTORY
  config
  DESTINATION share/${PROJECT_NAME}
)
install(DIRECTORY
  maps
  DESTINATION share/${PROJECT_NAME}
)

# map/ 和 rviz/ 以前没装。后果是 Run_nav.launch.py 的默认地图、以及
# `rviz2 -d $(ros2 pkg prefix racecar)/share/racecar/rviz/navigation.rviz` 这两条，
# 都得靠源码树正好在那儿才成立 —— 装完就找不到文件了。
install(DIRECTORY
  map
  rviz
  DESTINATION share/${PROJECT_NAME}
)

install(DIRECTORY
  launch
  DESTINATION share/${PROJECT_NAME}
)


ament_package()