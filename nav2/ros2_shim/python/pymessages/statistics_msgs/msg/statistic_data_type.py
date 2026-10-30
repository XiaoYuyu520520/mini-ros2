from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)

class StatisticDataType(Msg):
    STATISTICS_DATA_TYPE_UNINITIALIZED = 0
    STATISTICS_DATA_TYPE_AVERAGE = 1
    STATISTICS_DATA_TYPE_MINIMUM = 2
    STATISTICS_DATA_TYPE_MAXIMUM = 3
    STATISTICS_DATA_TYPE_STDDEV = 4
    STATISTICS_DATA_TYPE_SAMPLE_COUNT = 5

    def __init__(self):
        pass
