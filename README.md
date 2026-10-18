# SafeHaven

**SiWG917 SafeHaven: An Edge-AI Emergency Detection and Event-Triggered Evidence Capture System**

SafeHaven is a prototype that connects microphone audio captured by a Silicon Labs SiWG917 development kit to a host computer, where speech recognition can detect an emergency phrase and trigger event-based camera recording.

## Current Prototype Architecture

1. The SiWG917 firmware captures microphone audio and streams PCM audio over Wi-Fi using UDP.
2. A host-side audio receiver processes the incoming audio using speech recognition.
3. When the configured phrase is detected, the host sends a structured UDP event to the camera receiver.
4. The camera receiver validates the event fields and records a 10-second video clip using OpenCV.

**Implementation status:** In the current prototype, speech recognition and emergency-phrase detection run on the host computer. On-device keyword inference is a planned extension and is not claimed as integrated in the active firmware.

## Hardware

- Silicon Labs SiWG917 / SiWx917 development kit (BRD2605A).
- A computer running the host-side Python applications.
- A Wi-Fi access point.
- A webcam supported by OpenCV.

## Software

- Simplicity Studio and the Silicon Labs Wi-Fi SDK used by the firmware project.
- Python 3.
- OpenCV for camera capture.
- Vosk and an appropriate local speech-recognition model for host-side recognition.

## Repository Layout

- `firmware/` — firmware project and its project configuration.
- `host/safehaven_audio_receiver.py` — host-side audio reception and processing.
- `host/safehaven_receiver_reference.py` — reference camera-event receiver.
- `.gitignore` — excludes local environments, recordings, logs, and generated build output.

## Configuration

Before running the prototype, configure the firmware's Wi-Fi credentials and the destination computer's LAN IP address using the appropriate project configuration files. Keep real credentials private; do not commit personal Wi-Fi passwords, private keys, or other secrets.

The firmware streams audio to UDP port `5006`. The camera-event receiver listens on UDP port `5005`.

The host applications and the development kit must be on a network that permits the required UDP traffic. Configure the host firewall as needed for your local test environment.

## Running the Host Applications

1. Install Python 3 and the required Python dependencies.
2. Install a compatible Vosk speech-recognition model locally.
3. Configure the host receiver and firmware with the correct local network addresses and ports.
4. Start the host audio receiver.
5. Start the camera-event receiver.
6. Flash and run the firmware on the development kit.
7. Test with the configured phrase and confirm that an event is received and a video clip is recorded.

Inspect the Python source files for their actual command-line arguments and dependency requirements before running them; this README does not assume unverified command-line options.

## Event Validation

The camera-event receiver expects a JSON event with these fields:

- `type`: `SAFEHAVEN_VOICE_EVENT`
- `phrase`: `HELP_ME`
- `source`: `siwx917_microphone`
- `event_id`: an identifier containing 1–100 letters, digits, underscores, or hyphens

Only events matching the receiver's validation contract should trigger recording.

## Privacy and Safety

This prototype records video when a qualifying event is received. Camera access, network access, and recording permissions should be configured transparently and lawfully. Secure the local network and restrict access to recorded evidence.

The current UDP event mechanism does not provide authentication or encryption. It should not be treated as a production emergency-response or security system without additional security, reliability, and safety engineering.

## Attribution

The firmware project is based on a Silicon Labs Wi-Fi example and has been modified for the SafeHaven prototype. Preserve applicable original copyright notices and license terms in source files. The project includes Silicon Labs SDK components that may be governed by separate licenses; consult the relevant SDK and component license documentation before redistributing them.

## Project Status

This is a prototype. Validate firmware builds, audio delivery, phrase detection, event delivery, and camera recording on the intended hardware and host environment before relying on it.
