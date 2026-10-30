from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.builtin_interfaces.msg.time import Time

class ServiceEventInfo(Msg):
    REQUEST_SENT = 0
    REQUEST_RECEIVED = 1
    RESPONSE_SENT = 2
    RESPONSE_RECEIVED = 3
    _fields = [
        ('event_type', f_scalar('B')),
        ('stamp', f_msg(Time)),
        ('client_gid', f_array(f_scalar('B'), 16)),
        ('sequence_number', f_scalar('q')),
    ]

    def __init__(self):
        self.event_type = 0
        self.stamp = Time()
        self.client_gid = [0] * 16
        self.sequence_number = 0
