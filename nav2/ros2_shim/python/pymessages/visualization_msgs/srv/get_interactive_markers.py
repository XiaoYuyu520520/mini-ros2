from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.visualization_msgs.msg.interactive_marker import InteractiveMarker

class GetInteractiveMarkers:

    class Request(Msg):

        def __init__(self):
            pass

    class Response(Msg):
        _fields = [
            ('sequence_number', f_scalar('Q')),
            ('markers', f_seq(f_msg(InteractiveMarker))),
        ]

        def __init__(self):
            self.sequence_number = 0
            self.markers = []
