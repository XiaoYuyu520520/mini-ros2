from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)

class SaveMap:

    class Request(Msg):
        _fields = [
            ('map_topic', f_string()),
            ('map_url', f_string()),
            ('image_format', f_string()),
            ('map_mode', f_string()),
            ('free_thresh', f_scalar('f')),
            ('occupied_thresh', f_scalar('f')),
        ]

        def __init__(self):
            self.map_topic = ""
            self.map_url = ""
            self.image_format = ""
            self.map_mode = ""
            self.free_thresh = 0.0
            self.occupied_thresh = 0.0

    class Response(Msg):
        _fields = [
            ('result', f_bool()),
        ]

        def __init__(self):
            self.result = False
