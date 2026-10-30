from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.rosgraph_msgs.msg.service import Service
from pymessages.rosgraph_msgs.msg.topic import Topic

class Action(Msg):
    _fields = [
        ('name', f_string()),
        ('send_goal', f_msg(Service)),
        ('get_result', f_msg(Service)),
        ('cancel_goal', f_msg(Service)),
        ('feedback', f_msg(Topic)),
        ('status', f_msg(Topic)),
    ]

    def __init__(self):
        self.name = ""
        self.send_goal = Service()
        self.get_result = Service()
        self.cancel_goal = Service()
        self.feedback = Topic()
        self.status = Topic()
