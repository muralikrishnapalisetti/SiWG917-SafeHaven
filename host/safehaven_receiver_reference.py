
import cv2
import json
import socket
import time
import re
from datetime import datetime
from pathlib import Path
from collections import OrderedDict

# ---------------- CONFIGURATION ----------------

HOST = "0.0.0.0"
PORT = 5005

RECORD_SECONDS = 10
CAMERA_INDEX = 0
FPS = 20
FRAME_WIDTH = 640
FRAME_HEIGHT = 480

BASE_DIR = Path(__file__).resolve().parent
OUTPUT_DIR = BASE_DIR / "safehaven_events"
OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

LOG_FILE = OUTPUT_DIR / "event_log.jsonl"

# Remember recent event IDs to avoid duplicate recordings.
seen_events = OrderedDict()
MAX_SEEN_EVENTS = 500

# ---------------- EVENT VALIDATION ----------------

def validate_event(event):
    if not isinstance(event, dict):
        return False

    return (
        event.get("type") == "SAFEHAVEN_VOICE_EVENT"
        and event.get("phrase") == "HELP_ME"
        and event.get("source") == "siwx917_microphone"
        and isinstance(event.get("event_id"), str)
        and bool(re.fullmatch(r"[A-Za-z0-9_-]{1,100}",
                              event["event_id"]))
    )


def log_event(data):
    try:
        with open(LOG_FILE, "a", encoding="utf-8") as f:
            f.write(json.dumps(data) + "\n")
    except OSError as exc:
        print(f"[WARNING] Could not write event log: {exc}")


# ---------------- CAMERA SETUP ----------------

def open_camera():
    camera = cv2.VideoCapture(CAMERA_INDEX, cv2.CAP_DSHOW)

    if not camera.isOpened():
        camera.release()
        camera = cv2.VideoCapture(CAMERA_INDEX)

    if not camera.isOpened():
        raise RuntimeError(
            "Could not open webcam. Close other camera apps and try again."
        )

    camera.set(cv2.CAP_PROP_FRAME_WIDTH, FRAME_WIDTH)
    camera.set(cv2.CAP_PROP_FRAME_HEIGHT, FRAME_HEIGHT)
    camera.set(cv2.CAP_PROP_FPS, FPS)

    # Allow the camera to initialize and verify a frame.
    for _ in range(30):
        ok, frame = camera.read()
        if ok and frame is not None:
            return camera, frame
        time.sleep(0.05)

    camera.release()
    raise RuntimeError("Webcam opened, but no video frames were received.")


# ---------------- VIDEO RECORDING ----------------

def create_video_writer(event_id, frame):
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    safe_id = re.sub(r"[^A-Za-z0-9_-]", "_", event_id)
    filename = f"SafeHaven_{timestamp}_{safe_id}.avi"
    video_path = OUTPUT_DIR / filename

    height, width = frame.shape[:2]
    fourcc = cv2.VideoWriter_fourcc(*"MJPG")

    writer = cv2.VideoWriter(
        str(video_path),
        fourcc,
        FPS,
        (width, height)
    )

    if not writer.isOpened():
        writer.release()
        print("[ERROR] Could not create the video file.")
        return None, None

    return writer, video_path


def start_recording(event):
    event_id = event["event_id"]

    if event_id in seen_events:
        print(f"[INFO] Duplicate event ignored: {event_id}")
        return False

    seen_events[event_id] = time.time()

    while len(seen_events) > MAX_SEEN_EVENTS:
        seen_events.popitem(last=False)

    print("\n[SAFEHAVEN] HELP ME DETECTED")
    print("[SAFEHAVEN] Starting 10-second recording...")

    log_event({
        "time": datetime.now().isoformat(),
        "event_id": event_id,
        "transcript": event.get("transcript", ""),
        "status": "TRIGGER_RECEIVED"
    })

    return True


# ---------------- MAIN APPLICATION ----------------

def main():
    print("=" * 55)
    print("             SAFEHAVEN LIVE MONITOR")
    print("=" * 55)

    # Start event receiver.
    event_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    event_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    event_socket.bind((HOST, PORT))
    event_socket.setblocking(False)

    print(f"[READY] Listening for voice events on UDP port {PORT}")

    # Open webcam once and keep it open.
    try:
        camera, first_frame = open_camera()
    except RuntimeError as exc:
        event_socket.close()
        print(f"[CAMERA ERROR] {exc}")
        return

    print("[CAMERA] Live preview active.")
    print("[ARMED] Say 'help me' to start recording.")
    print("[CONTROL] Press Q in the camera window to exit.")

    writer = None
    video_path = None
    recording_until = 0.0
    current_event_id = None
    current_event = None
    last_message = 0.0

    window_name = "SafeHaven | Live Camera"
    cv2.namedWindow(window_name, cv2.WINDOW_NORMAL)

    try:
        while True:
            # Get the latest live camera frame.
            ok, frame = camera.read()
            if not ok or frame is None:
                print("[WARNING] Camera frame unavailable.")
                time.sleep(0.05)
                continue

            now = time.monotonic()

            # Check for incoming voice events without blocking the preview.
            while True:
                try:
                    payload, address = event_socket.recvfrom(8192)
                except BlockingIOError:
                    break
                except OSError:
                    break

                try:
                    event = json.loads(payload.decode("utf-8"))
                except (UnicodeDecodeError, json.JSONDecodeError):
                    print("[WARNING] Ignored invalid event message.")
                    continue

                if not validate_event(event):
                    print("[INFO] Ignored unrecognized event.")
                    continue

                # Do not restart the timer if a recording is already running.
                if writer is not None:
                    if time.time() - last_message > 1:
                        print("[INFO] Recording already in progress.")
                        last_message = time.time()
                    continue

                if start_recording(event):
                    writer, video_path = create_video_writer(
                        event["event_id"], frame
                    )

                    if writer is not None:
                        recording_until = now + RECORD_SECONDS
                        current_event_id = event["event_id"]
                        current_event = event

                        log_event({
                            "time": datetime.now().isoformat(),
                            "event_id": current_event_id,
                            "status": "RECORDING_STARTED",
                            "video": str(video_path)
                        })
                    else:
                        log_event({
                            "time": datetime.now().isoformat(),
                            "event_id": event["event_id"],
                            "status": "RECORDING_FAILED"
                        })

            # Recording is active: save frames while keeping preview visible.
            if writer is not None:
                writer.write(frame)
                remaining = max(0, recording_until - now)

                status = "RECORDING"
                status_color = (0, 0, 255)

                cv2.putText(
                    frame,
                    f"RECORDING: {remaining:.1f}s",
                    (20, 45),
                    cv2.FONT_HERSHEY_SIMPLEX,
                    0.9,
                    status_color,
                    2,
                    cv2.LINE_AA
                )

                cv2.circle(frame, (FRAME_WIDTH - 35, 35), 10,
                           status_color, -1)

                if now >= recording_until:
                    writer.release()
                    writer = None

                    print(f"[SAVED] Video: {video_path}")

                    log_event({
                        "time": datetime.now().isoformat(),
                        "event_id": current_event_id,
                        "status": "RECORDING_COMPLETED",
                        "video": str(video_path),
                        "duration_seconds": RECORD_SECONDS
                    })

                    video_path = None
                    current_event_id = None
                    current_event = None

            else:
                status = "ARMED - SAY HELP ME"
                status_color = (0, 200, 0)

                cv2.putText(
                    frame,
                    status,
                    (20, 45),
                    cv2.FONT_HERSHEY_SIMPLEX,
                    0.75,
                    status_color,
                    2,
                    cv2.LINE_AA
                )

            # Always show the current camera image.
            cv2.putText(
                frame,
                "SAFEHAVEN",
                (20, frame.shape[0] - 20),
                cv2.FONT_HERSHEY_SIMPLEX,
                0.65,
                (255, 255, 255),
                2,
                cv2.LINE_AA
            )

            cv2.imshow(window_name, frame)

            # Press Q while the camera window is focused to stop.
            if cv2.waitKey(1) & 0xFF in (ord("q"), ord("Q")):
                break

    except KeyboardInterrupt:
        print("\n[SAFEHAVEN] Stopping...")

    finally:
        if writer is not None:
            writer.release()

        camera.release()
        event_socket.close()
        cv2.destroyAllWindows()

        print("[SAFEHAVEN] Camera and event receiver closed.")
        print(f"[SAFEHAVEN] Files are stored in: {OUTPUT_DIR}")


if __name__ == "__main__":
    main()
