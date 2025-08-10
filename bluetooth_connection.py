import socket
import time
import re
import pygame
import zmq
import time
import math
import struct

# Normalize heading error to [-180, 180]
def normalize_angle(error):
    while error > 180:
        error -= 360
    while error < -180:
        error += 360
    return error

def get_controller_values(joystick, current_heading, desired_heading, last_last_current_heading):
    global lock, is_playing
    
    print(current_heading)

    if (joystick.get_axis(7) < 0.5):
        lock = False

    try:
        pygame.event.pump()
        base_throttle = int(round(joystick.get_axis(1) * -100))  # Negative to match forward direction
        esc3 = int(round(joystick.get_axis(2) * 100))

        auto_mode = joystick.get_axis(7) > 0.5

        if auto_mode:
            # Bang-bang parameters
            MAX_TURN_SPEED = 20  # Maximum differential throttle
            MIN_TURN_SPEED = 14  # Minimum to avoid stalling
            LARGE_ERROR_THRESHOLD = 30  # ° for max throttle
            SMALL_ERROR_THRESHOLD = 8  # ° for stop

            # Bang-bang control
            error = normalize_angle(desired_heading - current_heading)
            print(error)

            if lock:
                return [0, 0, esc3]

            if abs(error) > LARGE_ERROR_THRESHOLD:
                # Large error: apply max throttle
                correction = MAX_TURN_SPEED if error > 0 else -MAX_TURN_SPEED
                left_throttle = max(-100, min(100, int(round(base_throttle + correction))))
                right_throttle = max(-100, min(100, int(round(base_throttle + correction))))
                print(f"Turning to {desired_heading:.2f}° (current: {current_heading:.2f}°, Correction: {correction:.2f})")
            elif abs(error) > SMALL_ERROR_THRESHOLD:
                # Small error: apply min throttle to avoid stalling
                correction = MIN_TURN_SPEED if error > 0 else -MIN_TURN_SPEED
                left_throttle = max(-100, min(100, int(round(base_throttle + correction))))
                right_throttle = max(-100, min(100, int(round(base_throttle + correction))))
                print(f"Turning to {desired_heading:.2f}° (current: {current_heading:.2f}°, Correction: {correction:.2f})")
            #elif abs(last_last_current_heading - current_heading) < 5:
            else:
                # Target reached: stop turning
                left_throttle = 0
                right_throttle = 0
                print(f"Target heading {desired_heading:.2f}° reached (current: {current_heading:.2f}°)")
                lock = True
            return [left_throttle, right_throttle, esc3]
        else:
            x = int(round(joystick.get_axis(0) * 100))
            y = int(round(joystick.get_axis(1) * 100))
            z = int(round(joystick.get_axis(2) * 100))
            return [x, y, z]
    except pygame.error as e:
        print(f"Joystick error in get_controller_values: {e}")
        return [0, 0, 0]  # Safe default: stop robot

# Server settings
HOST = "0.0.0.0"# Listen on all interfaces
PORT = 80

# Create a UDP socket
server_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
server_socket.bind((HOST, PORT))
server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 6)
print(f"UDP server listening on {HOST}:{PORT}")

pygame.mixer.init()
lock_sound = pygame.mixer.Sound("lock_sound.mp3")
lock = False
is_playing = False

pygame.init()
pygame.joystick.init()

joystick = pygame.joystick.Joystick(0)
joystick.init()

current_heading = 0.0
desired_heading = 100
last_current_heading = current_heading
last_last_current_heading = last_current_heading

while True:
    try:
        #lock sound logic
        if lock and not is_playing:
            lock_sound.play()  # -1 loops forever
            is_playing = True

            # Stop playing if should_play is False and sound is playing
        elif not lock and is_playing:
            lock_sound.stop()
            is_playing = False

        last_current_heading = current_heading

        data, client_address = server_socket.recvfrom(1024)

        if len(data) >= 4:
            current_heading = struct.unpack('f', data[:4])[0]
            #print(f"Received heading from ESP32: {current_heading}")
        
        #print(current_heading)

        esc1, esc2, esc3 = get_controller_values(joystick, current_heading, desired_heading, last_last_current_heading)

        #print(f"ESC values: {esc1}, {esc2}, {esc3}")

        response = struct.pack('hhh', esc1, esc2, esc3)
        server_socket.sendto(response, client_address)
        server_socket.sendto(response, client_address)

        time.sleep(0.025)  # Adjust as needed for your application

    except Exception as e:
            print(f"Error: {e}")
        