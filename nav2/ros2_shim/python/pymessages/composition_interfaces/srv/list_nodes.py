from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)

class ListNodes:

    class Request(Msg):

        def __init__(self):
            pass

    class Response(Msg):
        _fields = [
            ('full_node_names', f_seq(f_string())),
            ('unique_ids', f_seq(f_scalar('Q'))),
        ]

        def __init__(self):
            self.full_node_names = []
            self.unique_ids = []
