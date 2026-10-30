from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.sensor_msgs.msg.image import Image
from pymessages.sensor_msgs.msg.region_of_interest import RegionOfInterest
from pymessages.std_msgs.msg.header import Header

class DisparityImage(Msg):
    _fields = [
        ('header', f_msg(Header)),
        ('image', f_msg(Image)),
        ('f', f_scalar('f')),
        ('t', f_scalar('f')),
        ('valid_window', f_msg(RegionOfInterest)),
        ('min_disparity', f_scalar('f')),
        ('max_disparity', f_scalar('f')),
        ('delta_d', f_scalar('f')),
    ]

    def __init__(self):
        self.header = Header()
        self.image = Image()
        self.f = 0.0
        self.t = 0.0
        self.valid_window = RegionOfInterest()
        self.min_disparity = 0.0
        self.max_disparity = 0.0
        self.delta_d = 0.0
