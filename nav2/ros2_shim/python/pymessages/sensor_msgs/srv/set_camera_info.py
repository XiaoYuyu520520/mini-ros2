from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.sensor_msgs.msg.camera_info import CameraInfo

class SetCameraInfo:

    class Request(Msg):
        _fields = [
            ('camera_info', f_msg(CameraInfo)),
        ]

        def __init__(self):
            self.camera_info = CameraInfo()

    class Response(Msg):
        _fields = [
            ('success', f_bool()),
            ('status_message', f_string()),
        ]

        def __init__(self):
            self.success = False
            self.status_message = ""
