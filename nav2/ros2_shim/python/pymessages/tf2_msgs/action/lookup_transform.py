from pymessages._runtime import (
    Msg, build_action_impl, f_array, f_bool, f_msg, f_scalar,
    f_seq, f_string, f_wstring,
)
from pymessages.builtin_interfaces.msg.duration import Duration
from pymessages.builtin_interfaces.msg.time import Time
from pymessages.geometry_msgs.msg.transform_stamped import TransformStamped
from pymessages.tf2_msgs.msg.tf2_error import TF2Error

class LookupTransform:

    class Goal(Msg):
        _fields = [
            ('target_frame', f_string()),
            ('source_frame', f_string()),
            ('source_time', f_msg(Time)),
            ('timeout', f_msg(Duration)),
            ('target_time', f_msg(Time)),
            ('fixed_frame', f_string()),
            ('advanced', f_bool()),
        ]

        def __init__(self):
            self.target_frame = ""
            self.source_frame = ""
            self.source_time = Time()
            self.timeout = Duration()
            self.target_time = Time()
            self.fixed_frame = ""
            self.advanced = False

    class Result(Msg):
        _fields = [
            ('transform', f_msg(TransformStamped)),
            ('error', f_msg(TF2Error)),
        ]

        def __init__(self):
            self.transform = TransformStamped()
            self.error = TF2Error()

    class Feedback(Msg):

        def __init__(self):
            pass

LookupTransform.Impl = build_action_impl(LookupTransform)
