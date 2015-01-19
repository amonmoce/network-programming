#!/usr/bin/env python

"""
A simple echo server
"""

import socket

host = ''
port = 33919
backlog = 10
size = 1024

s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind((host,port))
s.listen(backlog)

while 1:
    client, address = s.accept()
    data = client.recv(size)
    print 'data: ' + data,
    if data:
        client.send(data)
    client.close()
