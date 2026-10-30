from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.std_msgs.msg.header import Header

class FluidPressure(Msg):
    _fields = [
        ('header', f_msg(Header)),
        ('fluid_pressure', f_scalar('d')),
        ('variance', f_scalar('d')),
    ]

    def __init__(self):
        self.header = Header()
        self.fluid_pressure = 0.0
        self.variance = 0.0
