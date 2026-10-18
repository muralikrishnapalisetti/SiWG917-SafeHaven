# SafeHaven Firmware

## Project

**SafeHaven: Local-AI Emergency Phrase Detection and Event-Triggered Video Evidence Capture**

This firmware captures microphone audio on the Silicon Labs SiWG917 development kit and streams PCM audio to a host computer over Wi-Fi using UDP. Speech recognition currently runs on the host computer; on-device/MCU keyword inference is not integrated in the active firmware.

## Current Firmware Functionality

- Captures microphone audio using the configured board audio interface.
- Streams audio data to a configured host computer over Wi-Fi UDP.
- Uses UDP port `5006` for the host audio receiver.
- Relies on the host-side Python application for speech recognition and emergency-phrase detection.

Do not describe the active firmware as performing on-device AI inference unless that functionality is implemented and verified.

## Prototype Data Flow

1. The development kit captures microphone audio.
2. Firmware streams audio to the host computer over UDP.
3. `host/safehaven_audio_receiver.py` processes the audio using Vosk.
4. When the configured emergency phrase is detected, the host sends a JSON event to the camera-event receiver on UDP port `5005`.
5. `host/safehaven_receiver_reference.py` validates the event and attempts to record a 10-second video clip.

The host applications are maintained separately from this firmware project. See the repository root `README.md` for setup steps, architecture, and prototype status.

## Configuration

Before testing, configure the Wi-Fi SSID, Wi-Fi credentials, and host destination IP address in the appropriate firmware configuration source.

Use placeholders in published source code. Do not publish real Wi-Fi passwords, private keys, or other secrets.

Ensure the development kit and host computer can communicate over the local network. Configure the host firewall to permit the required UDP traffic.

## Building and Flashing

1. Open Simplicity Studio.
2. Import or open the firmware project using its `.slcp` project file.
3. Verify that the required Silicon Labs SDK components and toolchain are installed and correctly resolved.
4. Review the project configuration and network settings.
5. Build the project in Simplicity Studio.
6. Flash the resulting firmware to the supported development kit.
7. Start the host audio receiver and validate audio delivery.

The build and flash process must be verified in the target development environment. This document does not claim that a clean firmware build has been completed.

## Security and Limitations

The prototype uses UDP for audio and event delivery. UDP alone does not provide confidentiality, authentication, or integrity protection.

The camera-event receiver must not be treated as a production security or emergency-response service without additional authentication, encryption, reliability, privacy, and safety controls.

## Attribution

This project is based on a Silicon Labs Wi-Fi example. Preserve applicable original copyright notices and license terms. Review the relevant Silicon Labs SDK and third-party component licenses before redistributing SDK source files.

For the original example documentation, see `readme.md`.
