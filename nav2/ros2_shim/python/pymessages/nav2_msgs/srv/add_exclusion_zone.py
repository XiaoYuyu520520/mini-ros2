from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.nav2_msgs.msg.exclusion_zone_description import ExclusionZoneDescription

class AddExclusionZone:

    class Request(Msg):
        _fields = [
            ('zone', f_msg(ExclusionZoneDescription)),
        ]

        def __init__(self):
            self.zone = ExclusionZoneDescription()

    class Response(Msg):
        _fields = [
            ('success', f_bool()),
            ('message', f_string()),
        ]

        def __init__(self):
            self.success = False
            self.message = ""
