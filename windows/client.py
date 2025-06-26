import socket
import struct
import numpy as np
import cv2
import json
import time
import threading

video_sock = None
input_sock = None

is_getting_vid_sock = False
is_getting_in_sock = False

VIDEO_PORT = 12345
INPUT_PORT = 12346
SERVER_IP = '192.168.0.26'  # Change this to the Linux server's IP

RECONNECT_DELAY = 2  # seconds
window_name = "Remote Window"
click_position = None

# --- Mouse callback function ---
def mouse_callback(event, x, y, flags, param):
    global click_position
    if event == cv2.EVENT_LBUTTONDOWN:
        click_position = {"type": "click", "button": "left", "x": x, "y": y}
    elif event == cv2.EVENT_RBUTTONDOWN:
        click_position = {"type": "click", "button": "right", "x": x, "y": y}
    elif event == cv2.EVENT_LBUTTONDBLCLK:
        click_position = {"type": "dclick", "button": "left", "x": x, "y": y}

def connect_video():
    global video_sock,is_getting_vid_sock
    is_getting_vid_sock = True
    while video_sock is None:
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.connect((SERVER_IP, VIDEO_PORT))
            video_sock = s
            print("[CLIENT] Connected to video stream")
        except Exception:
            time.sleep(RECONNECT_DELAY)
    is_getting_vid_sock = False
def connect_input():
    global input_sock,is_getting_in_sock
    is_getting_in_sock = True
    while input_sock is None:
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.connect((SERVER_IP, INPUT_PORT))
            input_sock = s
            print("[CLIENT] Connected to input control")
        except Exception:
            time.sleep(RECONNECT_DELAY)
    is_getting_in_sock = False
# --- Setup OpenCV ---

threading.Thread(target=connect_video, daemon=True).start()
threading.Thread(target=connect_input, daemon=True).start()

# --- Main loop ---
while True:
    while(video_sock == None or  input_sock == None):
        time.sleep(0.1)
    cv2.namedWindow(window_name)
    cv2.setMouseCallback(window_name, mouse_callback)
    data = b''
    payload_size = 4

    try:
        while True:
            # --- Receive frame size ---
            while len(data) < payload_size:
                try:
                    packet = video_sock.recv(4096)
                except ConnectionAbortedError:
                    pass
                if not packet:
                    raise ConnectionError("Video socket disconnected")
                data += packet

            packed_size = data[:payload_size]
            data = data[payload_size:]
            frame_size = struct.unpack('!I', packed_size)[0]

            # --- Receive frame data ---
            while len(data) < frame_size:
                packet = video_sock.recv(4096)
                if not packet:
                    raise ConnectionError("Video socket disconnected")
                data += packet

            frame_data = data[:frame_size]
            data = data[frame_size:]

            # --- Decode and display frame ---
            frame = cv2.imdecode(np.frombuffer(frame_data, np.uint8), cv2.IMREAD_COLOR)
            if frame is not None:
                cv2.imshow(window_name, frame)

            # --- Handle mouse click send ---
            if click_position:
                msg = json.dumps(click_position).encode('utf-8')
                msg_len = struct.pack('!I', len(msg))
                input_sock.sendall(msg_len + msg)
                click_position = None

            # --- Handle key input ---
            key = cv2.waitKey(1) & 0xFF
            if key == ord('q'):
                raise KeyboardInterrupt
            elif key != 255:
                key_event = {"type": "key", "key": chr(key)}
                msg = json.dumps(key_event).encode('utf-8')
                msg_len = struct.pack('!I', len(msg))
                input_sock.sendall(msg_len + msg)

    except (ConnectionError, OSError,ConnectionAbortedError) as e:
        print(f"[CLIENT] Disconnected or error: {e}")
        # cv2.imshow(window_name, np.zeros((200, 400, 3), dtype=np.uint8))
        # try:
        #     cv2.displayOverlay(window_name, "Disconnected. Reconnecting...", 2000)
        # except Exception as e:
        #     pass
        cv2.destroyAllWindows()
        video_sock.close()
        input_sock.close()
        video_sock = None
        input_sock = None
        if(video_sock == None and is_getting_vid_sock == False):
            threading.Thread(target=connect_video, daemon=True).start()
        if(input_sock == None and is_getting_in_sock == False):
            threading.Thread(target=connect_input, daemon=True).start()
        time.sleep(RECONNECT_DELAY)
        continue

    except KeyboardInterrupt:
        print("[CLIENT] Exiting...")
        break

# --- Clean exit ---
cv2.destroyAllWindows()
try: video_sock.close()
except: pass
try: input_sock.close()
except: pass
