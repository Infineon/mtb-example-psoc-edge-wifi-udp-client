#******************************************************************************
# File Name:   udp_server.py
#
# Description: A simple "udp server" for demonstrating UDP usage.
# The server sends LED ON/OFF commands to the connected UDP client
# and receives acknowledgement from the client.
#
#********************************************************************************
#  (c) 2024-2026, Infineon Technologies AG, or an affiliate of Infineon
#  Technologies AG. All rights reserved.
#  This software, associated documentation and materials ("Software") is
#  owned by Infineon Technologies AG or one of its affiliates ("Infineon")
#  and is protected by and subject to worldwide patent protection, worldwide
#  copyright laws, and international treaty provisions. Therefore, you may use
#  this Software only as provided in the license agreement accompanying the
#  software package from which you obtained this Software. If no license
#  agreement applies, then any use, reproduction, modification, translation, or
#  compilation of this Software is prohibited without the express written
#  permission of Infineon.
#  
#  Disclaimer: UNLESS OTHERWISE EXPRESSLY AGREED WITH INFINEON, THIS SOFTWARE
#  IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
#  INCLUDING, BUT NOT LIMITED TO, ALL WARRANTIES OF NON-INFRINGEMENT OF
#  THIRD-PARTY RIGHTS AND IMPLIED WARRANTIES SUCH AS WARRANTIES OF FITNESS FOR A
#  SPECIFIC USE/PURPOSE OR MERCHANTABILITY.
#  Infineon reserves the right to make changes to the Software without notice.
#  You are responsible for properly designing, programming, and testing the
#  functionality and safety of your intended application of the Software, as
#  well as complying with any legal requirements related to its use. Infineon
#  does not guarantee that the Software will be free from intrusion, data theft
#  or loss, or other breaches ("Security Breaches"), and Infineon shall have
#  no liability arising out of any Security Breaches. Unless otherwise
#  explicitly approved by Infineon, the Software may not be used in any
#  application where a failure of the Product or any consequences of the use
#  thereof can reasonably be expected to result in personal injury.
#********************************************************************************

#!/usr/bin/python

import socket
import optparse
import time
import sys

DEFAULT_IP   = socket.gethostbyname(socket.gethostname())   # IP address of the UDP server
DEFAULT_PORT = 50007         # Port of the UDP server

LED_ON = '1'
LED_OFF = '0'

def enter_command(sock, addr):
    print("============================================================")
    cmd = int(input('Enter 0 to turn off or 1 to turn on the LED on Client\nInput Command: '))
    if cmd == 1:
        sock.sendto(bytes(LED_ON, "utf-8"), addr)
    elif cmd == 0:
        sock.sendto(bytes(LED_OFF, "utf-8"), addr)
    else:
        sock.sendto(bytes(str(cmd), "utf-8"), addr)

def echo_server(host, port):
    print("============================================================")
    print("UDP Server")
    print("============================================================")
    # Create a UDP socket
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    #Bind UDP Server
    sock.bind((host,port))
    print('UDP Server on IP Address: {} port {}'.format(host, port))
    print('waiting to receive message from UDP Client')
    
    while True:
        data, addr = sock.recvfrom(4096)
        if data == bytes('A', "utf-8"):
            print('Ready to send command to client')
            enter_command(sock, addr)
        elif data == bytes('LED ON ACK', "utf-8"):
            print("LED ON Acknowledgement received")
            enter_command(sock, addr)
        elif data == bytes('LED OFF ACK', "utf-8"):
            print("LED OFF Acknowledgement received")
            enter_command(sock, addr)
        else:
            print("Message from Client: {}".format(data))
            enter_command(sock, addr)
        

if __name__ == '__main__':
    parser = optparse.OptionParser()
    parser.add_option("-p", "--port", dest="port", type="int", default=DEFAULT_PORT, help="Port to listen on [default: %default].")
    parser.add_option("--hostname", dest="hostname", default=DEFAULT_IP, help="Hostname to listen on.")

    (options, args) = parser.parse_args()

    echo_server(options.hostname, options.port)
