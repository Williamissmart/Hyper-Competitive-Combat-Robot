import cv2
import time
import math
import zmq
from ultralytics import YOLO

# Load your model (change path if needed)
model = YOLO("best.pt")  # or yolov12.pt if using that version

# Set up camera
cap = cv2.VideoCapture("Video\GX010158.MP4")
#cap.set(cv2.CAP_PROP_FRAME_WIDTH, 960)
#cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 540)
cv2.namedWindow("YOLO Detection")

#setup communications
context = zmq.Context()
socket = context.socket(zmq.PUSH)
socket.bind("tcp://127.0.0.1:5555")

class_names = model.names

while True:
    ret, frame = cap.read()
    if not ret:
        break

    a_center = None
    b_center = None

    # Run YOLO on the masked frame
    results = model(frame, device='cuda', conf=0.5)

    # Draw bounding boxes and centers
    if results and results[0].boxes is not None:
        boxes = results[0].boxes.xyxy.cpu().numpy()
        class_ids = results[0].boxes.cls.cpu().numpy().astype(int)

        detected_objects = []

        for box, class_id in zip(boxes, class_ids):
            x1, y1, x2, y2 = map(int, box)
            center_x = int((x1 + x2) / 2)
            center_y = int((y1 + y2) / 2)
            label = class_names[int(class_id)]

            class_name = class_names.get(int(class_id), str(class_id))

            # Draw bounding box
            cv2.rectangle(frame, (x1, y1), (x2, y2), (0, 255, 0), 2)
            # Draw center dot
            cv2.circle(frame, (center_x, center_y), 5, (0, 0, 255), -1)
            #Draw class label
            cv2.putText(frame, f"{class_name}", (x1, y1 - 10),
                cv2.FONT_HERSHEY_SIMPLEX, 0.6, (255, 255, 255), 2)

            print(f"Detection: {class_name} | Box: ({x1}, {y1}) to ({x2}, {y2}) | Center: ({center_x}, {center_y})")

            detected_objects.append({
                    "label": label,
                    "center": (center_x, center_y)
                })

            # Extract centers for class "a" and "b"
            

            for obj in detected_objects:
                if obj['label'] == 'O Invincible':
                    a_center = obj['center']
                elif obj['label'] == 'Shelmet':
                    b_center = obj['center']

            

    # Compute angle if both detected
    if a_center and b_center:

        x1, y1 = a_center
        x2, y2 = b_center

        # Draw line between A and B
        cv2.line(frame, (x1, y1), (x2, y2), (255, 0, 0), 2)

        # Compute angle from A to B
        dx = x2 - x1
        dy = y2 - y1
        angle_rad = math.atan2(dy, dx)
        angle_deg = math.degrees(angle_rad)

        angle_deg = abs((angle_deg + 360) % 360)

        angle_text = f"{angle_deg:.1f}°"
        
        # Normalize angle to 0–360 if preferred

        socket.send_json(angle_deg)

        arrow_length = 50  # pixels
        end_x = int(x1 + arrow_length * math.cos(angle_rad))
        end_y = int(y1 + arrow_length * math.sin(angle_rad))
        cv2.arrowedLine(frame, (x1, y1), (end_x, end_y), (0, 255, 255), 3, tipLength=0.3)

        # Put text near the midpoint of the line
        mid_x = int((x1 + x2) / 2)
        mid_y = int((y1 + y2) / 2)
        cv2.putText(frame, angle_text, (mid_x + 10, mid_y - 10),
                cv2.FONT_HERSHEY_SIMPLEX, 0.6, (255, 255, 255), 2)
        


    # Show the frame
    cv2.imshow("YOLO Detection", frame)
    cv2.waitKey(1)


    time.sleep(0.01)

cap.release()
cv2.destroyAllWindows()