from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.builtin_interfaces.msg.time import Time
from pymessages.statistics_msgs.msg.statistic_data_point import StatisticDataPoint

class MetricsMessage(Msg):
    _fields = [
        ('measurement_source_name', f_string()),
        ('metrics_source', f_string()),
        ('unit', f_string()),
        ('window_start', f_msg(Time)),
        ('window_stop', f_msg(Time)),
        ('statistics', f_seq(f_msg(StatisticDataPoint))),
    ]

    def __init__(self):
        self.measurement_source_name = ""
        self.metrics_source = ""
        self.unit = ""
        self.window_start = Time()
        self.window_stop = Time()
        self.statistics = []
