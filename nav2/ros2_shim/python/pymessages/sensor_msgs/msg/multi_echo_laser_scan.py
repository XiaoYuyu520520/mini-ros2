from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.sensor_msgs.msg.laser_echo import LaserEcho
from pymessages.std_msgs.msg.header import Header

class MultiEchoLaserScan(Msg):
    _fields = [
        ('header', f_msg(Header)),
        ('angle_min', f_scalar('f')),
        ('angle_max', f_scalar('f')),
        ('angle_increment', f_scalar('f')),
        ('time_increment', f_scalar('f')),
        ('scan_time', f_scalar('f')),
        ('range_min', f_scalar('f')),
        ('range_max', f_scalar('f')),
        ('ranges', f_seq(f_msg(LaserEcho))),
        ('intensities', f_seq(f_msg(LaserEcho))),
    ]

    def __init__(self):
        self.header = Header()
        self.angle_min = 0.0
        self.angle_max = 0.0
        self.angle_increment = 0.0
        self.time_increment = 0.0
        self.scan_time = 0.0
        self.range_min = 0.0
        self.range_max = 0.0
        self.ranges = []
        self.intensities = []
