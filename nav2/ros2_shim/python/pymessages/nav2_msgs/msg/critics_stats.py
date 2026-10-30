from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.builtin_interfaces.msg.time import Time

class CriticsStats(Msg):
    _fields = [
        ('stamp', f_msg(Time)),
        ('critics', f_seq(f_string())),
        ('changed', f_seq(f_bool())),
        ('costs_sum', f_seq(f_scalar('f'))),
    ]

    def __init__(self):
        self.stamp = Time()
        self.critics = []
        self.changed = []
        self.costs_sum = []
