from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.type_description_interfaces.msg.field_type import FieldType

class Field(Msg):
    _fields = [
        ('name', f_string()),
        ('type', f_msg(FieldType)),
        ('default_value', f_string()),
    ]

    def __init__(self):
        self.name = ""
        self.type = FieldType()
        self.default_value = ""
