from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.unique_identifier_msgs.msg.uuid import UUID

class RemoveShapes:

    class Request(Msg):
        _fields = [
            ('all_objects', f_bool()),
            ('uuids', f_seq(f_msg(UUID))),
        ]

        def __init__(self):
            self.all_objects = False
            self.uuids = []

    class Response(Msg):
        _fields = [
            ('success', f_bool()),
        ]

        def __init__(self):
            self.success = False
