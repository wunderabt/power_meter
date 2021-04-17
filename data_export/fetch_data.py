"""
script to fetch hex values from gateway
"""

import socket
import time
import logging

logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)

def fetch_data() -> bytes:
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.connect(('192.168.178.177', 8888))
        sock.send(b"hello") # any message works
        sock.settimeout(10) # wait max for N seconds
        blocksize = 1024
        while(True):
            try:
                data = sock.recv(blocksize).replace(b'\r\n', b'\n').replace(bytes.fromhex("05"), b'')
                yield data
                if data.endswith(b"EOT"):
                    break
            except socket.timeout:
                logger.error("Transmission timeout")
                break # looks like there is no more data

def fetch_and_write(outfile: str):
    with open(outfile, "wb") as dumpfile:
        total_bytes = 0
        time1 = time.time()
        for data in fetch_data():
            dumpfile.write(data)
            total_bytes += len(data)
            if time.time() - time1 > 2: # print progress every 2s
                logger.info("{:8d}k".format(total_bytes//1024))
                time1 = time.time()

    logger.info("wrote %sk to %s", total_bytes//1024, outfile)

if __name__ == "__main__":
    fetch_and_write("dump.hex")