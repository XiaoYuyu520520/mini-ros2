from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)

class TypeSource(Msg):
    _fields = [
        ('type_name', f_string()),
        ('encoding', f_string()),
        ('raw_file_contents', f_string()),
    ]

    def __init__(self):
        self.type_name = ""
        self.encoding = ""
        self.raw_file_contents = ""
