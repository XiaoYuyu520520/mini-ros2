from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)

class RegionOfInterest(Msg):
    _fields = [
        ('x_offset', f_scalar('I')),
        ('y_offset', f_scalar('I')),
        ('height', f_scalar('I')),
        ('width', f_scalar('I')),
        ('do_rectify', f_bool()),
    ]

    def __init__(self):
        self.x_offset = 0
        self.y_offset = 0
        self.height = 0
        self.width = 0
        self.do_rectify = False
