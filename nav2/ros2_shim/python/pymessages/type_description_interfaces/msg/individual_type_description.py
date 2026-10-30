from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.type_description_interfaces.msg.field import Field

class IndividualTypeDescription(Msg):
    _fields = [
        ('type_name', f_string()),
        ('fields', f_seq(f_msg(Field))),
    ]

    def __init__(self):
        self.type_name = ""
        self.fields = []
