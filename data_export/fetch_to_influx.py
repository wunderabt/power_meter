"""
script to fetch data from gateway and upload to influxdb
"""
import io
from fetch_data import fetch_data
from dump_to_influx import dump_to_influx


buffer = io.BytesIO()

for data in fetch_data():
    buffer.write(data)

buffer.seek(0)

dump_to_influx(io.TextIOWrapper(buffer))