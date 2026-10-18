
import json
import socket
import time
import wave
import array
import sys
from pathlib import Path

from vosk import Model, KaldiRecognizer


# ============================================================
# SAFEHAVEN CONFIGURATION
# ============================================================

HOST = "0.0.0.0"
AUDIO_PORT = 5006

SAMPLE_RATE = 16000
BYTES_PER_SAMPLE = 2
CHANNELS = 1

EVENT_HOST = "127.0.0.1"
EVENT_PORT = 5005

BASE_DIR = Path(__file__).resolve().parent
MODEL_PATH = BASE_DIR / "vosk-model-small-en-us-0.15"

RAW_WAV_PATH = BASE_DIR / "safehaven_raw.wav"
WAV_PATH = BASE_DIR / "safehaven_test.wav"

# Keep the current gain while evaluating recognition.
# Increasing gain blindly can amplify noise and cause distortion.
AUDIO_GAIN = 4.0

TRIGGER_COOLDOWN = 10.0
DIAGNOSTIC_INTERVAL = 25

# A partial result must contain "help me" for this many
# consecutive packet checks before triggering early.
PARTIAL_CONFIRMATION_CHECKS = 3

# Supported spoken command variations.
COMMAND_PHRASES = [
    "help me",
    "please help me",
    "help me please",
]

COMMAND_GRAMMAR = json.dumps(COMMAND_PHRASES)


# ============================================================
# LOAD VOSK MODEL
# ============================================================

if not (MODEL_PATH / "am").is_dir():
    raise FileNotFoundError(
        f"Vosk model not found or incomplete: {MODEL_PATH}"
    )

print("[SafeHaven] Loading Vosk model...", flush=True)

model = Model(str(MODEL_PATH))

recognizer = KaldiRecognizer(
    model,
    SAMPLE_RATE,
    COMMAND_GRAMMAR
)

recognizer.SetWords(True)

print("[SafeHaven] Vosk model loaded successfully.", flush=True)
print("[SafeHaven] Command grammar enabled.", flush=True)
print(
    "[SafeHaven] Accepted commands: "
    + ", ".join(COMMAND_PHRASES),
    flush=True
)


# ============================================================
# UDP SOCKETS
# ============================================================

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 256 * 1024)
sock.bind((HOST, AUDIO_PORT))
sock.settimeout(1.0)

event_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)


# ============================================================
# AUDIO RECORDING FILES
# ============================================================

raw_wav = wave.open(str(RAW_WAV_PATH), "wb")
raw_wav.setnchannels(CHANNELS)
raw_wav.setsampwidth(BYTES_PER_SAMPLE)
raw_wav.setframerate(SAMPLE_RATE)

enhanced_wav = wave.open(str(WAV_PATH), "wb")
enhanced_wav.setnchannels(CHANNELS)
enhanced_wav.setsampwidth(BYTES_PER_SAMPLE)
enhanced_wav.setframerate(SAMPLE_RATE)


# ============================================================
# STATUS
# ============================================================

print(f"[SafeHaven] Listening on UDP port {AUDIO_PORT}", flush=True)
print(
    f"[SafeHaven] Expected audio: {SAMPLE_RATE} Hz, "
    "mono, signed 16-bit PCM",
    flush=True
)
print(f"[Recording] Raw audio: {RAW_WAV_PATH}", flush=True)
print(f"[Recording] Enhanced audio: {WAV_PATH}", flush=True)
print(f"[Audio] Software gain: {AUDIO_GAIN}x", flush=True)
print("[SafeHaven] Say 'help me' to trigger SafeHaven.", flush=True)
print("[SafeHaven] Press Ctrl+C to stop and save diagnostics.", flush=True)


# ============================================================
# RUNTIME DIAGNOSTICS AND TRIGGER STATE
# ============================================================

packet_count = 0
total_bytes = 0
last_stats_time = time.monotonic()

last_trigger = -TRIGGER_COOLDOWN
last_partial = ""

partial_candidate_count = 0
partial_candidate_text = ""

max_raw_rms = 0
max_enhanced_rms = 0
max_raw_peak = 0
clipped_samples_total = 0


# ============================================================
# HELPER FUNCTIONS
# ============================================================

def normalize_text(text):
    """Normalize recognition text for command matching."""
    return " ".join(text.lower().strip().split())


def contains_help_me(text):
    """
    Match the complete two-word command.
    Do not trigger on the word 'help' alone.
    """
    words = normalize_text(text).split()
    return any(
        words[i] == "help" and words[i + 1] == "me"
        for i in range(len(words) - 1)
    )


def send_voice_event(transcript):
    """Send a SafeHaven event compatible with the webcam receiver."""
    global last_trigger

    now = time.monotonic()

    if now - last_trigger < TRIGGER_COOLDOWN:
        return False

    last_trigger = now

    event = {
        "type": "SAFEHAVEN_VOICE_EVENT",
        "event_id": f"voice-{time.time_ns()}",
        "phrase": "HELP_ME",
        "transcript": transcript,
        "source": "siwx917_microphone"
    }

    print("\n[ALERT] HELP ME DETECTED", flush=True)
    print(
        "[SafeHaven] Sending event: " + json.dumps(event),
        flush=True
    )

    try:
        sent = event_sock.sendto(
            json.dumps(event).encode("utf-8"),
            (EVENT_HOST, EVENT_PORT)
        )

        print(
            f"[SafeHaven] Event sent successfully ({sent} bytes).",
            flush=True
        )
        print(
            "[SafeHaven] Webcam receiver should begin recording.",
            flush=True
        )
        return True

    except OSError as exc:
        print(
            f"[Error] Could not send voice event: {exc}",
            flush=True
        )
        return False


def process_partial_result():
    """
    Check partial speech recognition.

    Early triggering requires the full 'help me' phrase to remain
    present across multiple consecutive packet checks.
    """
    global last_partial
    global partial_candidate_count
    global partial_candidate_text

    partial_result = json.loads(recognizer.PartialResult())
    partial = normalize_text(partial_result.get("partial", ""))

    if partial and partial != last_partial:
        print(f"[Partial] {partial}", flush=True)
        last_partial = partial

    if contains_help_me(partial):
        if partial_candidate_text == partial:
            partial_candidate_count += 1
        else:
            partial_candidate_text = partial
            partial_candidate_count = 1

        if partial_candidate_count >= PARTIAL_CONFIRMATION_CHECKS:
            print(
                "[Recognition] Stable partial command confirmed.",
                flush=True
            )
            send_voice_event(partial)

            # Prevent the same partial hypothesis from retriggering.
            partial_candidate_count = 0
            partial_candidate_text = ""

    else:
        partial_candidate_count = 0
        partial_candidate_text = ""


# ============================================================
# MAIN AUDIO RECEIVE AND RECOGNITION LOOP
# ============================================================

try:
    while True:

        # ----------------------------------------------------
        # Receive audio packet
        # ----------------------------------------------------

        try:
            packet, address = sock.recvfrom(8192)

        except socket.timeout:
            now = time.monotonic()

            if packet_count == 0:
                print(
                    "[SafeHaven] Waiting for UDP audio packets...",
                    flush=True
                )
            elif now - last_stats_time >= 5.0:
                print(
                    f"[UDP] Packets={packet_count}, bytes={total_bytes}",
                    flush=True
                )
                last_stats_time = now

            continue

        if not packet:
            continue

        if len(packet) % BYTES_PER_SAMPLE != 0:
            print(
                f"[Warning] Invalid PCM packet length: {len(packet)}",
                flush=True
            )
            continue

        packet_count += 1
        total_bytes += len(packet)

        if packet_count == 1:
            print(
                f"[UDP] First packet from {address[0]}:{address[1]} "
                f"({len(packet)} bytes)",
                flush=True
            )
            print("[SafeHaven] Audio stream active.", flush=True)

        if packet_count % DIAGNOSTIC_INTERVAL == 0:
            print(
                f"[UDP] Packet #{packet_count}: {len(packet)} bytes",
                flush=True
            )

        # ----------------------------------------------------
        # Decode signed 16-bit little-endian PCM
        # ----------------------------------------------------

        samples = array.array("h")
        samples.frombytes(packet)

        if sys.byteorder != "little":
            samples.byteswap()

        sample_count = len(samples)

        if sample_count == 0:
            continue

        # ----------------------------------------------------
        # Measure raw audio
        # ----------------------------------------------------

        raw_peak = max(abs(sample) for sample in samples)

        raw_mean_square = (
            sum(sample * sample for sample in samples)
            / sample_count
        )

        raw_rms = int(raw_mean_square ** 0.5)

        max_raw_rms = max(max_raw_rms, raw_rms)
        max_raw_peak = max(max_raw_peak, raw_peak)

        # Preserve the exact incoming PCM audio.
        raw_wav.writeframesraw(packet)

        # ----------------------------------------------------
        # Apply software gain with signed 16-bit saturation
        # ----------------------------------------------------

        enhanced_samples = array.array("h")
        clipped_this_packet = 0

        for sample in samples:
            amplified = round(sample * AUDIO_GAIN)

            if amplified > 32767 or amplified < -32768:
                clipped_this_packet += 1

            amplified = max(-32768, min(32767, amplified))
            enhanced_samples.append(amplified)

        clipped_samples_total += clipped_this_packet

        if sys.byteorder != "little":
            enhanced_samples.byteswap()

        enhanced_data = enhanced_samples.tobytes()
        enhanced_wav.writeframesraw(enhanced_data)

        enhanced_mean_square = (
            sum(sample * sample for sample in enhanced_samples)
            / sample_count
        )

        enhanced_rms = int(enhanced_mean_square ** 0.5)
        max_enhanced_rms = max(max_enhanced_rms, enhanced_rms)

        if packet_count == 1 or packet_count % DIAGNOSTIC_INTERVAL == 0:
            print(
                f"[Audio] Raw RMS={raw_rms}, "
                f"raw peak={raw_peak}, "
                f"enhanced RMS={enhanced_rms}, "
                f"clipped samples={clipped_this_packet}",
                flush=True
            )

        # ----------------------------------------------------
        # Speech recognition
        # ----------------------------------------------------

        if recognizer.AcceptWaveform(enhanced_data):

            result = json.loads(recognizer.Result())
            phrase = normalize_text(result.get("text", ""))

            last_partial = ""
            partial_candidate_count = 0
            partial_candidate_text = ""

            if phrase:
                print(f"[Speech - Final] {phrase}", flush=True)
            else:
                print(
                    "[Speech - Final] No command recognized.",
                    flush=True
                )

            # Final results are also checked, in case the phrase
            # was not stable long enough for the partial trigger.
            if contains_help_me(phrase):
                send_voice_event(phrase)

        else:
            process_partial_result()


# ============================================================
# SHUTDOWN AND SAVE DIAGNOSTICS
# ============================================================

except KeyboardInterrupt:
    print("\n[SafeHaven] Receiver stopped by user.", flush=True)

except Exception as exc:
    print(
        f"[Error] Receiver failed: {type(exc).__name__}: {exc}",
        flush=True
    )
    raise

finally:
    try:
        raw_wav.close()
        enhanced_wav.close()

        print(
            f"[Recording] Raw WAV saved: {RAW_WAV_PATH}",
            flush=True
        )
        print(
            f"[Recording] Enhanced WAV saved: {WAV_PATH}",
            flush=True
        )

        print(
            f"[Diagnostic] Packets={packet_count}, bytes={total_bytes}",
            flush=True
        )
        print(
            f"[Diagnostic] Maximum raw RMS={max_raw_rms}, "
            f"maximum enhanced RMS={max_enhanced_rms}, "
            f"maximum raw peak={max_raw_peak}",
            flush=True
        )
        print(
            "[Diagnostic] Total clipped samples after gain="
            f"{clipped_samples_total}",
            flush=True
        )

    finally:
        sock.close()
        event_sock.close()
        print("[SafeHaven] UDP sockets closed.", flush=True)
