from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)

class State(Msg):
    PRIMARY_STATE_UNKNOWN = 0
    PRIMARY_STATE_UNCONFIGURED = 1
    PRIMARY_STATE_INACTIVE = 2
    PRIMARY_STATE_ACTIVE = 3
    PRIMARY_STATE_FINALIZED = 4
    TRANSITION_STATE_CONFIGURING = 10
    TRANSITION_STATE_CLEANINGUP = 11
    TRANSITION_STATE_SHUTTINGDOWN = 12
    TRANSITION_STATE_ACTIVATING = 13
    TRANSITION_STATE_DEACTIVATING = 14
    TRANSITION_STATE_ERRORPROCESSING = 15
    _fields = [
        ('id', f_scalar('B')),
        ('label', f_string()),
    ]

    def __init__(self):
        self.id = 0
        self.label = ""
