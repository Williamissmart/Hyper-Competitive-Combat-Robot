import zmq
import time
context = zmq.Context()
socket = context.socket(zmq.PUSH)
socket.bind("tcp://127.0.0.1:5555")

angle_deg = 0
socket.send_json(angle_deg)

while True:
    socket.send_json(angle_deg)
    angle_deg = angle_deg + 10
    time.sleep(2)
    print(f"Sent angle: {angle_deg} degrees")