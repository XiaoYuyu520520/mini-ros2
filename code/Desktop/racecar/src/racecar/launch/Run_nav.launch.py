import os
from ament_index_python.packages import get_package_share_directory
from launch_ros.actions import Node
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, IncludeLaunchDescription
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import LaunchConfiguration

def generate_launch_description():
    use_sim_time = LaunchConfiguration('use_sim_time', default='false')

        
    # nav_dir = get_package_share_directory('racecar')
    # nav_launchr = os.path.join(nav_dir, 'launch')

    racecar_dir = get_package_share_directory('racecar')
    racecar_launchr = os.path.join(racecar_dir, 'launch')

    # 默认地图取自包自己的 share 目录（CMakeLists 把 map/ 装了进去），不再是真机上那条
    # /root/Desktop/racecar/... 绝对路径 —— 那条只在车上的源码树里成立，装完之后
    # `ros2 launch` 是找不到这个文件的。`racecar_ctl.py nav` 无论怎样都会传 `map:=`，
    # 这里管的是裸启动那一次。
    map_file = LaunchConfiguration(
        'map',
        default=os.path.join(racecar_dir, 'map', 'ai_map.yaml')
    )
 
    param_dir = os.path.join(racecar_dir, 'config')
    param_file = LaunchConfiguration('params', default=os.path.join(
        param_dir, 'nav.yaml'))


    return LaunchDescription([
        DeclareLaunchArgument(
            'map',
            default_value=map_file,
            description='Full path to map file to load'),

        DeclareLaunchArgument(
            'params',
            default_value=param_file,
            description='Full path to param file to load'),
        Node(
            name='waypoint_cycle',
            package='nav2_waypoint_cycle',
            executable='nav2_waypoint_cycle',
        ),

        IncludeLaunchDescription(
            PythonLaunchDescriptionSource(
                [racecar_launchr, '/bringup_launch.py']),
            launch_arguments={
                'map': map_file,
                'use_sim_time': use_sim_time,
                'params_file': param_file}.items(),
        ),

    ])



# 上面那条坑已经修掉了：`os.path.join(map_dir, '/某绝对路径')` 里，后一个参数以 / 开头时
# `os.path.join` 会把前面的全部丢掉，于是不管 map_dir 是什么都指向那个绝对路径 —— 而且指的
# 还是**源码目录**，不是 install 目录。现在 map 和 params 两个默认值都取 share 目录下的相对位置。
