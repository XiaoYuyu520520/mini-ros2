from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.sensor_msgs.msg.region_of_interest import RegionOfInterest
from pymessages.std_msgs.msg.header import Header

class CameraInfo(Msg):
    _fields = [
        ('header', f_msg(Header)),
        ('height', f_scalar('I')),
        ('width', f_scalar('I')),
        ('distortion_model', f_string()),
        ('d', f_seq(f_scalar('d'))),
        ('k', f_array(f_scalar('d'), 9)),
        ('r', f_array(f_scalar('d'), 9)),
        ('p', f_array(f_scalar('d'), 12)),
        ('binning_x', f_scalar('I')),
        ('binning_y', f_scalar('I')),
        ('roi', f_msg(RegionOfInterest)),
    ]

    def __init__(self):
        self.header = Header()
        self.height = 0
        self.width = 0
        self.distortion_model = ""
        self.d = []
        self.k = [0.0] * 9
        self.r = [0.0] * 9
        self.p = [0.0] * 12
        self.binning_x = 0
        self.binning_y = 0
        self.roi = RegionOfInterest()
