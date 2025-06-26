import socket
import struct
import numpy as np
import cv2
import json

VIDEO_PORT = 12345
INPUT_PORT = 12346
SERVER_IP = '192.168.0.26'  # Change to your Linux machine IP

video_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
input_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

print("[CLIENT] Connecting to video stream...")
video_sock.connect((SERVER_IP, VIDEO_PORT))
print("[CLIENT] Connecting to input control...")
input_sock.connect((SERVER_IP, INPUT_PORT))

# Shared state
data = b''
payload_size = 4
click_position = None
window_name = "Remote Window"

# Mouse callback
def mouse_callback(event, x, y, flags, param):
    global click_position
    # print(event)
    if event == cv2.EVENT_LBUTTONDOWN:
        click_position = {"type": "click", "button": "left", "x": x, "y": y}
    elif event == cv2.EVENT_RBUTTONDOWN:
        click_position = {"type": "click", "button": "right", "x": x, "y": y}
    elif event == cv2.EVENT_LBUTTONDBLCLK:
        click_position = {"type": "dclick", "button": "left", "x": x, "y": y}
    # print(click_position)
# Setup OpenCV window
cv2.namedWindow(window_name)
cv2.setMouseCallback(window_name, mouse_callback)

while True:
    # --- Receive frame size ---
    while len(data) < payload_size:
        packet = video_sock.recv(4096)
        if not packet:
            break
        data += packet
    if len(data) < payload_size:
        break

    packed_size = data[:payload_size]
    data = data[payload_size:]
    frame_size = struct.unpack('!I', packed_size)[0]

    # --- Receive frame data ---
    while len(data) < frame_size:
        packet = video_sock.recv(4096)
        if not packet:
            break
        data += packet
    if len(data) < frame_size:
        break

    frame_data = data[:frame_size]
    data = data[frame_size:]

    # --- Decode and display frame ---
    frame = cv2.imdecode(np.frombuffer(frame_data, np.uint8), cv2.IMREAD_COLOR)
    if frame is not None:
        cv2.imshow(window_name, frame)

    # --- Send click data if available ---
    if click_position:
        msg = json.dumps(click_position).encode('utf-8')
        msg_len = struct.pack('!I', len(msg))
        input_sock.sendall(msg_len + msg)
        click_position = None

    key = cv2.waitKey(1) & 0xFF
    if key == ord('q'):
        break
    elif key != 255:  # Ignore "no key"
        key_event = {"type": "key", "key": chr(key)}
        msg = json.dumps(key_event).encode('utf-8')
        msg_len = struct.pack('!I', len(msg))
        input_sock.sendall(msg_len + msg)

        

video_sock.close()
input_sock.close()
cv2.destroyAllWindows()
