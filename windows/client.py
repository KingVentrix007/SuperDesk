import socket
import struct
import numpy as np
import cv2

SERVER_IP = '192.168.0.26'  # Change this
PORT = 12345

sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.connect((SERVER_IP, PORT))

data = b''
payload_size = 4

while True:
    # Receive frame size
    while len(data) < payload_size:
        packet = sock.recv(4096)
        if not packet:
            break
        data += packet
    if len(data) < payload_size:
        break

    packed_size = data[:payload_size]
    data = data[payload_size:]
    frame_size = struct.unpack('!I', packed_size)[0]

    # Receive frame data
    while len(data) < frame_size:
        packet = sock.recv(4096)
        if not packet:
            break
        data += packet
    if len(data) < frame_size:
        break

    frame_data = data[:frame_size]
    data = data[frame_size:]

    # Decode JPEG
    frame = cv2.imdecode(np.frombuffer(frame_data, np.uint8), cv2.IMREAD_COLOR)
    if frame is None:
        continue

    cv2.imshow('Remote Window', frame)
    if cv2.waitKey(1) & 0xFF == ord('q'):
        break

sock.close()
cv2.destroyAllWindows()
