
bytestr = b''
with open("serial-dump.hex", "rt") as hex_dump_fh:
    for line in hex_dump_fh:
        for hex in line.strip().split(' '):
            bytestr += bytes.fromhex("{:>02s}".format(hex))

with open("serial-dump.bin", "wb") as bin_dump_fh:
    bin_dump_fh.write(bytestr)