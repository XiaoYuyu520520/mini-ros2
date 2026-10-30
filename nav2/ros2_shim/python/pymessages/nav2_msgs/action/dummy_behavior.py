from pymessages._runtime import (
    Msg, build_action_impl, f_array, f_bool, f_msg, f_scalar,
    f_seq, f_string, f_wstring,
)
from pymessages.builtin_interfaces.msg.duration import Duration
from pymessages.std_msgs.msg.string import String

class DummyBehavior:

    class Goal(Msg):
        _fields = [
            ('command', f_msg(String)),
        ]

        def __init__(self):
            self.command = String()

    class Result(Msg):
        _fields = [
            ('total_elapsed_time', f_msg(Duration)),
            ('error_code', f_scalar('H')),
            ('error_msg', f_string()),
        ]

        def __init__(self):
            self.total_elapsed_time = Duration()
            self.error_code = 0
            self.error_msg = ""

    class Feedback(Msg):

        def __init__(self):
            pass

DummyBehavior.Impl = build_action_impl(DummyBehavior)
