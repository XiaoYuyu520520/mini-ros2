from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)

class ManageLifecycleNodes:

    class Request(Msg):
        STARTUP = 0
        PAUSE = 1
        RESUME = 2
        RESET = 3
        SHUTDOWN = 4
        CONFIGURE = 5
        CLEANUP = 6
        _fields = [
            ('command', f_scalar('B')),
        ]

        def __init__(self):
            self.command = 0

    class Response(Msg):
        _fields = [
            ('success', f_bool()),
        ]

        def __init__(self):
            self.success = False
