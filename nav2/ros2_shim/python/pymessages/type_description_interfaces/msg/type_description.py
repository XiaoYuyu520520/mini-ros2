from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.type_description_interfaces.msg.individual_type_description import IndividualTypeDescription

class TypeDescription(Msg):
    _fields = [
        ('type_description', f_msg(IndividualTypeDescription)),
        ('referenced_type_descriptions', f_seq(f_msg(IndividualTypeDescription))),
    ]

    def __init__(self):
        self.type_description = IndividualTypeDescription()
        self.referenced_type_descriptions = []
