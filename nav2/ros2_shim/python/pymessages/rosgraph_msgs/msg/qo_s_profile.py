from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.builtin_interfaces.msg.duration import Duration

class QoSProfile(Msg):
    HISTORY_SYSTEM_DEFAULT = 0
    HISTORY_KEEP_LAST = 1
    HISTORY_KEEP_ALL = 2
    HISTORY_UNKNOWN = 3
    RELIABILITY_SYSTEM_DEFAULT = 0
    RELIABILITY_RELIABLE = 1
    RELIABILITY_BEST_EFFORT = 2
    RELIABILITY_UNKNOWN = 3
    RELIABILITY_BEST_AVAILABLE = 4
    DURABILITY_SYSTEM_DEFAULT = 0
    DURABILITY_TRANSIENT_LOCAL = 1
    DURABILITY_VOLATILE = 2
    DURABILITY_UNKNOWN = 3
    DURABILITY_BEST_AVAILABLE = 4
    LIVELINESS_SYSTEM_DEFAULT = 0
    LIVELINESS_AUTOMATIC = 1
    LIVELINESS_MANUAL_BY_TOPIC = 3
    LIVELINESS_UNKNOWN = 4
    LIVELINESS_BEST_AVAILABLE = 5
    _fields = [
        ('depth', f_scalar('I')),
        ('deadline', f_msg(Duration)),
        ('lifespan', f_msg(Duration)),
        ('history', f_scalar('B')),
        ('reliability', f_scalar('B')),
        ('durability', f_scalar('B')),
        ('liveliness', f_scalar('B')),
        ('liveliness_lease_duration', f_msg(Duration)),
    ]

    def __init__(self):
        self.depth = 0
        self.deadline = Duration()
        self.lifespan = Duration()
        self.history = 0
        self.reliability = 0
        self.durability = 0
        self.liveliness = 0
        self.liveliness_lease_duration = Duration()
