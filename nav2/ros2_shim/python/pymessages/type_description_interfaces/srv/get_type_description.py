from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.type_description_interfaces.msg.key_value import KeyValue
from pymessages.type_description_interfaces.msg.type_description import TypeDescription
from pymessages.type_description_interfaces.msg.type_source import TypeSource

class GetTypeDescription:

    class Request(Msg):
        _fields = [
            ('type_name', f_string()),
            ('type_hash', f_string()),
            ('include_type_sources', f_bool()),
        ]

        def __init__(self):
            self.type_name = ""
            self.type_hash = ""
            self.include_type_sources = False

    class Response(Msg):
        _fields = [
            ('successful', f_bool()),
            ('failure_reason', f_string()),
            ('type_description', f_msg(TypeDescription)),
            ('type_sources', f_seq(f_msg(TypeSource))),
            ('extra_information', f_seq(f_msg(KeyValue))),
        ]

        def __init__(self):
            self.successful = False
            self.failure_reason = ""
            self.type_description = TypeDescription()
            self.type_sources = []
            self.extra_information = []
