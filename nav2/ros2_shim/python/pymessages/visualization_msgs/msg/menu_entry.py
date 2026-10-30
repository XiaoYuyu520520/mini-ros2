from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)

class MenuEntry(Msg):
    FEEDBACK = 0
    ROSRUN = 1
    ROSLAUNCH = 2
    _fields = [
        ('id', f_scalar('I')),
        ('parent_id', f_scalar('I')),
        ('title', f_string()),
        ('command', f_string()),
        ('command_type', f_scalar('B')),
    ]

    def __init__(self):
        self.id = 0
        self.parent_id = 0
        self.title = ""
        self.command = ""
        self.command_type = 0
