"""
script to load the dump.hex into influxdb
"""
import collections
from dump_to_csv import valid_triplet, triplet_to_tuple
from influxdb import InfluxDBClient

client = InfluxDBClient("troi", 8086, "influxdb", "influxdb", "power")

def dump_to_influx(dump_file):
    triplet = collections.deque([[], [], []], maxlen=3)
    rec_cnt = 0
    for line in dump_file:
        triplet.append(line.strip().split(' '))
        if valid_triplet(triplet):
            timestamp, energy, batvolt = triplet_to_tuple(triplet)
            points = [{ "measurement": "energy",
                        "tags": { "device": "ISKRA MT-631" },
                        "fields": { "Wh": round(energy) },
                        "time": timestamp.isoformat()
                      },
                      { "measurement": "voltage",
                        "tags": { "device": "adafruit feather 32u4" },
                        "fields": { "V": batvolt },
                        "time": timestamp.isoformat()
                      }]
            client.write_points(points)
            rec_cnt += 1
    print("Wrote {} records to influxdb".format(rec_cnt))


if __name__ == "__main__":
    with open("dump.hex") as dfh:
      dump_to_influx(dfh)
