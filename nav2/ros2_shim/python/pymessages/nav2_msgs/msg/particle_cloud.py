from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.nav2_msgs.msg.particle import Particle
from pymessages.std_msgs.msg.header import Header

class ParticleCloud(Msg):
    _fields = [
        ('header', f_msg(Header)),
        ('particles', f_seq(f_msg(Particle))),
    ]

    def __init__(self):
        self.header = Header()
        self.particles = []
