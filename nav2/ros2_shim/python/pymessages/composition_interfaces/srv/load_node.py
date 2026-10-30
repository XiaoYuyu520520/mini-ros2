from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.rcl_interfaces.msg.parameter import Parameter

class LoadNode:

    class Request(Msg):
        _fields = [
            ('package_name', f_string()),
            ('plugin_name', f_string()),
            ('node_name', f_string()),
            ('node_namespace', f_string()),
            ('log_level', f_scalar('B')),
            ('remap_rules', f_seq(f_string())),
            ('parameters', f_seq(f_msg(Parameter))),
            ('extra_arguments', f_seq(f_msg(Parameter))),
        ]

        def __init__(self):
            self.package_name = ""
            self.plugin_name = ""
            self.node_name = ""
            self.node_namespace = ""
            self.log_level = 0
            self.remap_rules = []
            self.parameters = []
            self.extra_arguments = []

    class Response(Msg):
        _fields = [
            ('success', f_bool()),
            ('error_message', f_string()),
            ('full_node_name', f_string()),
            ('unique_id', f_scalar('Q')),
        ]

        def __init__(self):
            self.success = False
            self.error_message = ""
            self.full_node_name = ""
            self.unique_id = 0
