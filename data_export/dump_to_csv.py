"""
script to read dump.hex and convert to csv
contains all conversion function from hex to power and timestamps
"""

import collections
import datetime

def hexlist_to_int(val: list) -> int:
    hexval = "".join(["{:>02s}".format(x) for x in val])
    return int.from_bytes(bytes.fromhex(hexval), "big", signed=True)

def get_battery_voltage(val: list) -> float:
    # val has 2 elements in little endian
    rawval = hexlist_to_int(reversed(val)) # hexlist_to_int expects big endian
    return (rawval*2.0*3.3)/1024 # adafruit specific conversion, see manual

def get_field_idx(val: list) -> list:
    # returns a list with tuples with (idx, length)
    skip_next = 0
    field_idxs = []
    for i, sml_byte in enumerate(val):
        if skip_next > 0:
            skip_next -= 1
            continue
        if sml_byte.startswith('6') or sml_byte.startswith('5'):
            len = int(sml_byte[1])
            field_idxs.append((i, len))
            skip_next = len-1
    return field_idxs

def get_power(val: list) -> float:
    # val is something like ['65', '0', '10', '1', '4', '1', '62', '1E', '52', '3', '62', '38', '1', '77', '7', '1']
    # each "interesting" field starts with 6n or 5n where n is the length of the field
    field_idxs = get_field_idx(val)
    # fields are
    #  0 - status
    #  1 - unit, 1E = Wh
    #  2 - scaler, 3 = k
    #  3 - value
    # get scaler
    scaler_idx, scaler_length = field_idxs[2]
    scaler = hexlist_to_int(val[scaler_idx+1:scaler_idx+scaler_length])
    # get power
    power_idx, power_length = field_idxs[3]
    power = hexlist_to_int(val[power_idx+1:power_idx+power_length])
    return float(power*(10**scaler))

def get_timestamp(val: list) -> datetime:
    # val is something like ['72', '62', '1', '65', '0', '15', '43', 'C1', '74', '77', '7', '1']
    # we're interested in the 2nd field that starts with 6n
    field_idxs = get_field_idx(val)
    uptime_idx, uptime_length = field_idxs[1]
    uptime = hexlist_to_int(val[uptime_idx+1:uptime_idx+uptime_length])
    install_date = datetime.datetime.fromisoformat('2021-03-19T11:10:00')
    return install_date + datetime.timedelta(seconds=uptime)

def valid_triplet(triplet: collections.deque) -> bool:
    # a valid triplet has a
    #  1. timestamp (12 bytes)
    #  2. power reading (16 bytes)
    #  3. battery level (2 bytes)
    return len(triplet[0]) == 12 and \
           (len(triplet[1]) == 16 or len(triplet[1]) == 20) and \
           len(triplet[2]) == 2

def triplet_to_tuple(triplet: collections.deque) -> str:
    timestamp = get_timestamp(triplet[0])
    power = get_power(triplet[1])
    bat_volts = get_battery_voltage(triplet[2])
    return (timestamp, power, bat_volts)

def dump_to_csv(dump_file: str) -> str:
    triplet = collections.deque([[], [], []], maxlen=3)
    with open(dump_file, "rt") as dfh:
        for line in dfh:
            triplet.append(line.strip().split(' '))
            if valid_triplet(triplet):
                yield ",".join((str(x) for x in triplet_to_tuple(triplet)))

if __name__ == "__main__":
    for line in list(dump_to_csv("dump.hex")):
        print(line)