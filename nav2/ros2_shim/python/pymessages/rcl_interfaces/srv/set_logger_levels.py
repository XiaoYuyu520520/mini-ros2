from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.rcl_interfaces.msg.logger_level import LoggerLevel
from pymessages.rcl_interfaces.msg.set_logger_levels_result import SetLoggerLevelsResult

class SetLoggerLevels:

    class Request(Msg):
        _fields = [
            ('levels', f_seq(f_msg(LoggerLevel))),
        ]

        def __init__(self):
            self.levels = []

    class Response(Msg):
        _fields = [
            ('results', f_seq(f_msg(SetLoggerLevelsResult))),
        ]

        def __init__(self):
            self.results = []
