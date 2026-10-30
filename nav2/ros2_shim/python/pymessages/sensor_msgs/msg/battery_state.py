from pymessages._runtime import (
    Msg, f_array, f_bool, f_msg, f_scalar, f_seq, f_string, f_wstring,
)
from pymessages.std_msgs.msg.header import Header

class BatteryState(Msg):
    POWER_SUPPLY_STATUS_UNKNOWN = 0
    POWER_SUPPLY_STATUS_CHARGING = 1
    POWER_SUPPLY_STATUS_DISCHARGING = 2
    POWER_SUPPLY_STATUS_NOT_CHARGING = 3
    POWER_SUPPLY_STATUS_FULL = 4
    POWER_SUPPLY_HEALTH_UNKNOWN = 0
    POWER_SUPPLY_HEALTH_GOOD = 1
    POWER_SUPPLY_HEALTH_OVERHEAT = 2
    POWER_SUPPLY_HEALTH_DEAD = 3
    POWER_SUPPLY_HEALTH_OVERVOLTAGE = 4
    POWER_SUPPLY_HEALTH_UNSPEC_FAILURE = 5
    POWER_SUPPLY_HEALTH_COLD = 6
    POWER_SUPPLY_HEALTH_WATCHDOG_TIMER_EXPIRE = 7
    POWER_SUPPLY_HEALTH_SAFETY_TIMER_EXPIRE = 8
    POWER_SUPPLY_TECHNOLOGY_UNKNOWN = 0
    POWER_SUPPLY_TECHNOLOGY_NIMH = 1
    POWER_SUPPLY_TECHNOLOGY_LION = 2
    POWER_SUPPLY_TECHNOLOGY_LIPO = 3
    POWER_SUPPLY_TECHNOLOGY_LIFE = 4
    POWER_SUPPLY_TECHNOLOGY_NICD = 5
    POWER_SUPPLY_TECHNOLOGY_LIMN = 6
    POWER_SUPPLY_TECHNOLOGY_TERNARY = 7
    POWER_SUPPLY_TECHNOLOGY_VRLA = 8
    _fields = [
        ('header', f_msg(Header)),
        ('voltage', f_scalar('f')),
        ('temperature', f_scalar('f')),
        ('current', f_scalar('f')),
        ('charge', f_scalar('f')),
        ('capacity', f_scalar('f')),
        ('design_capacity', f_scalar('f')),
        ('percentage', f_scalar('f')),
        ('power_supply_status', f_scalar('B')),
        ('power_supply_health', f_scalar('B')),
        ('power_supply_technology', f_scalar('B')),
        ('present', f_bool()),
        ('cell_voltage', f_seq(f_scalar('f'))),
        ('cell_temperature', f_seq(f_scalar('f'))),
        ('location', f_string()),
        ('serial_number', f_string()),
    ]

    def __init__(self):
        self.header = Header()
        self.voltage = 0.0
        self.temperature = 0.0
        self.current = 0.0
        self.charge = 0.0
        self.capacity = 0.0
        self.design_capacity = 0.0
        self.percentage = 0.0
        self.power_supply_status = 0
        self.power_supply_health = 0
        self.power_supply_technology = 0
        self.present = False
        self.cell_voltage = []
        self.cell_temperature = []
        self.location = ""
        self.serial_number = ""
