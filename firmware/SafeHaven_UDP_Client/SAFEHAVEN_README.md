# SafeHaven Firmware Guide

## 1. Purpose

This directory contains the firmware project for the **Silicon Labs SiWG917 / SiWx917 development kit** used in SafeHaven.

The active firmware is responsible for microphone capture and streaming audio samples to a host computer over Wi-Fi UDP. Speech recognition and the emergency-phrase decision currently happen on the host computer in the Python/Vosk application. **This firmware README does not claim that on-device keyword spotting or Edge AI inference is integrated.**

For the full system explanation, Windows setup instructions, network architecture, event format, troubleshooting, and validation guidance, see the repository root `README.md`.

## 2. Firmware Responsibilities

The firmware application in `app.c` is intended to:

1. Initialize the Wi-Fi station and the configured network profile.
2. Initialize and read microphone samples through the configured SiWG917 microphone/I2S interfaces.
3. convert the captured sample buffer into a signed 16-bit mono audio buffer.
4. Send audio data to the host computer using a UDP socket on port `5006`.
5. Print diagnostic messages for destination configuration, microphone initialization, sample capture, packet transmission, and errors.

The host computer must be running `host/safehaven_audio_receiver.py` and reachable at the configured LAN IPv4 address. The host-side speech recognizer expects 16 kHz, mono, signed 16-bit PCM.

## 3. Firmware Project Files

- `SafeHaven_UDP_Client.slcp` — Simplicity Studio project description.
- `app.c` and `app.h` — SafeHaven application implementation and declarations.
- `main.c` — application entry-point integration.
- `config/` — board, microphone, I2S, Wi-Fi and other application configuration headers.
- `autogen/` — project-generated source and configuration artifacts; these should be kept consistent with the selected SDK/project configuration.
- `cmake_gcc/` — generated/configured build-system files for the GCC build flow.
- `readme.md` — inherited documentation from the original Silicon Labs Wi-Fi example.
- `SAFEHAVEN_README.md` — this project-focused guide.

The firmware is based on a Silicon Labs Wi-Fi example. Preserve source-file copyright and license notices and review applicable SDK/component licenses before redistributing SDK material.

## 4. Network Configuration

### Host destination address

In `app.c`, the audio destination is currently set through:

```c
#define SERVER_IP_ADDRESS "HOST IP ADDRESS HERE"
#define AUDIO_SERVER_PORT 5006U
```

Replace the placeholder with your host PC's LAN IPv4 address. For example, `192.168.1.25` is an example address only; use the address assigned to the network interface shared with the development kit.

Do not use `127.0.0.1` for the board's destination address. Loopback refers to the device itself and will not route the board's packets to the host PC.

### Wi-Fi client credentials

The default client profile is defined in `config/sl_net_default_values.h`:

```c
#define DEFAULT_WIFI_CLIENT_PROFILE_SSID "Your_wifi_name"
#define DEFAULT_WIFI_CLIENT_CREDENTIAL "Your_wifi_password"
```

These are placeholders. Set credentials using the configuration mechanism selected by your Simplicity Studio project. Do not commit real Wi-Fi passwords, keys, tokens, or other secrets to version control.

### Host firewall and ports

- **UDP 5006:** audio packets flow from the development kit to the host audio receiver.
- **UDP 5005:** the host audio receiver sends local JSON events to the camera receiver at `127.0.0.1:5005`. This event port is used by the host applications, not by the firmware's audio socket.

Keep the board and host computer connected to a network that allows local UDP traffic. Permit inbound UDP 5006 through the host firewall only as needed for the trusted test network.

## 5. Build and Flash Workflow

1. Install Simplicity Studio and a compatible Silicon Labs SDK/toolchain for the selected SiWG917 target.
2. Open or import `SafeHaven_UDP_Client.slcp`.
3. Confirm the required components resolve in the IDE and that the board/target selection is correct.
4. Configure the host IP address and Wi-Fi profile.
5. Build the project and inspect any compiler, linker, SDK-resolution, or configuration errors.
6. Flash the firmware to the development kit.
7. Open the board log/serial output and check Wi-Fi connection, microphone initialization, sample capture, socket setup, and packet-send diagnostics.
8. Start the host audio receiver and confirm that its packet counter advances after the board begins streaming.

**Build status:** A clean firmware build and a fresh board-side end-to-end test must be verified in the intended Simplicity Studio environment. Do not infer a successful firmware build merely from the presence of generated files or successful host-side tests.

## 6. Audio Format and Important Validation Note

The host-side audio receiver expects:

- Sample rate: **16,000 samples/second**
- Channels: **mono**
- Sample representation: **signed 16-bit PCM**
- Transport: **UDP port 5006**

The current firmware code captures audio into a buffer of 32-bit stereo samples and selects channel 0 while shifting each value to form a 16-bit mono sample. The source comments identify this as a test conversion and explicitly state the assumption that the valid sample bits occupy the upper 16 bits of each 32-bit I2S word.

That assumption needs to be confirmed against the actual SiWx917 I2S receive/sample representation. With the physical board available, validate sample alignment, channel choice, signal amplitude, clipping, and intelligibility before treating the stream as fully verified. The host receiver's RMS/peak diagnostics and WAV captures can help with this check.

## 7. Relationship to the Host Applications

The complete current prototype uses two host-side Python applications:

1. `host/safehaven_audio_receiver.py` loads Vosk, receives the incoming audio stream, and checks for `help me`, `please help me`, or `help me please`.
2. When a phrase is detected, the audio receiver sends a JSON event to `127.0.0.1:5005`.
3. `host/safehaven_receiver_reference.py` checks the event fields and attempts to record a 10-second webcam clip.

Start the camera receiver before the audio receiver so the event listener and webcam are ready. The root README documents the exact Windows commands and expected console messages.

## 8. Security and Safety

The current UDP transport does not provide application-level authentication, encryption, replay protection, or integrity protection. Input-field validation on the camera receiver helps reject malformed events but does not prove who sent a message.

Use the prototype only on a trusted test network. Keep credentials out of Git, restrict access to recorded clips and logs, and make camera use transparent and lawful. SafeHaven is an experimental project; it is not a certified emergency-response system and must not be the only way to request help.

## 9. Known Limitations and Next Steps

- On-device/MCU keyword-spotting inference is not integrated in the active firmware.
- The microphone/I2S-to-PCM conversion assumption requires confirmation on the actual board.
- Firmware build/flash and end-to-end board-to-host validation must be recorded from a real hardware test.
- UDP messages are not authenticated or encrypted.
- Detection reliability under different voices, noise conditions and network disruptions has not been characterized here.

Suggested next steps are to validate the captured PCM format on hardware, measure packet loss and phrase-trigger performance, evaluate a compact on-device keyword-spotting model against the SiWG917's resource constraints, and add authenticated/replay-resistant event delivery before any production-oriented use.

## 10. Original Example Documentation

The inherited `readme.md` describes the original Silicon Labs Wi-Fi power-save example, including its Energy Profiler and current-measurement procedures. Those instructions are retained as source-example documentation and **are not evidence that SafeHaven power measurements have been performed**.
