from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)

class GetParameterTypes:

    class Request(Msg):
        _fields = [
            ('names', f_seq(f_string())),
        ]

        def __init__(self):
            self.names = []

    class Response(Msg):
        _fields = [
            ('types', f_seq(f_scalar('B'))),
        ]

        def __init__(self):
            self.types = []
