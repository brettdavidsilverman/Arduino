# A simple example that:
# - Connects to a WiFi Network defined by "ssid" and "password"
# - Performs a GET request (loads a webpage)
# - Queries the current time from a server
import machine
import network   # handles connecting to WiFi
import urequests # handles making and servicing network requests
import time
import socket

# Connect to network
wlan = network.WLAN(network.STA_IF)
wlan.active(True)

# Fill in your network name (ssid) and password here:
ssid = 'bee-cam-1'
password = 'feebeegeeb3'
wlan.connect(ssid, password)

# Wait for connection to establish
max_wait = 10
while max_wait > 0:
    if wlan.status() < 0 or wlan.status() >= 3:
            break
    max_wait -= 1
    print('waiting for connection...')
    time.sleep(1)
    
# Manage connection errors
if wlan.status() != 3:
    raise RuntimeError('Network Connection has failed')
else:
    print('Connected')
    
print(wlan.ifconfig())
ipAddress = wlan.ifconfig()[2]

    # The real mccoy
print("\n\n3. Querying bee-cam-1")
url = "http://" + ipAddress + ":81/stream"
print(url)

sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.connect((ipAddress, 81))
cmd = 'GET /stream HTTP/1.0\r\n\r\n'.encode()
sock.send(cmd)
while True:
    data = sock.recv(512) 
    if (len(data)) < 1:
        break 
    print(data) 

sock.close() 


#response = urequests.get(url)
#print(r.content())
