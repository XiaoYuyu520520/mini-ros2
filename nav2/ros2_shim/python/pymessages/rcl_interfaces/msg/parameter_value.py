from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)

class ParameterValue(Msg):
    _fields = [
        ('type', f_scalar('B')),
        ('bool_value', f_bool()),
        ('integer_value', f_scalar('q')),
        ('double_value', f_scalar('d')),
        ('string_value', f_string()),
        ('byte_array_value', f_seq(f_scalar('B'))),
        ('bool_array_value', f_seq(f_bool())),
        ('integer_array_value', f_seq(f_scalar('q'))),
        ('double_array_value', f_seq(f_scalar('d'))),
        ('string_array_value', f_seq(f_string())),
    ]

    def __init__(self):
        self.type = 0
        self.bool_value = False
        self.integer_value = 0
        self.double_value = 0.0
        self.string_value = ""
        self.byte_array_value = []
        self.bool_array_value = []
        self.integer_array_value = []
        self.double_array_value = []
        self.string_array_value = []
