from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)

class PointField(Msg):
    INT8 = 1
    UINT8 = 2
    INT16 = 3
    UINT16 = 4
    INT32 = 5
    UINT32 = 6
    FLOAT32 = 7
    FLOAT64 = 8
    INT64 = 9
    UINT64 = 10
    BOOL = 11
    _fields = [
        ('name', f_string()),
        ('offset', f_scalar('I')),
        ('datatype', f_scalar('B')),
        ('count', f_scalar('I')),
    ]

    def __init__(self):
        self.name = ""
        self.offset = 0
        self.datatype = 0
        self.count = 0
