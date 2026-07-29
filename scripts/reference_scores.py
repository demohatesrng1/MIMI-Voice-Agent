#!/usr/bin/env python3
"""Dump per-frame openWakeWord scores so the C++ port can be checked against them.

Not part of the build. openWakeWord is the reference implementation for the
wake-word frontend, and the C++ in src/voice/wake_word.cpp has to reproduce its
melspectrogram framing, its x/10+2 transform and its 76/8/16 buffer geometry
exactly. This writes a TSV that tools/mimi_wake --compare diffs against.

    python3 scripts/reference_scores.py testdata/hey_jarvis.wav hey_jarvis

Note: openWakeWord primes its feature buffer with 4 seconds of *random* noise,
so the first 16 frames (1.28 s) can never match bit-for-bit. Comparison starts
after the buffer has filled with real audio.
"""

import sys
import wave

import numpy as np
from openwakeword.model import Model

CHUNK = 1280  # 80 ms at 16 kHz -- openWakeWord's native frame


def read_wav(path):
    with wave.open(path, "rb") as w:
        if w.getframerate() != 16000 or w.getnchannels() != 1 or w.getsampwidth() != 2:
            raise SystemExit(
                f"{path}: need 16 kHz mono 16-bit, got {w.getframerate()} Hz "
                f"{w.getnchannels()}ch {w.getsampwidth() * 8}-bit"
            )
        return np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16)


def main():
    if len(sys.argv) < 3:
        raise SystemExit("usage: reference_scores.py WAV.wav MODEL_KEY [MODEL.onnx]")
    wav_path, key = sys.argv[1], sys.argv[2]
    model_path = sys.argv[3] if len(sys.argv) > 3 else f"models/{key}_v0.1.onnx"

    audio = read_wav(wav_path)
    oww = Model(wakeword_models=[model_path], inference_framework="onnx")
    name = next(iter(oww.models.keys()))

    print(f"# {wav_path}  model={name}  frames={len(audio) // CHUNK}", file=sys.stderr)
    print("frame\tscore")
    for i in range(len(audio) // CHUNK):
        chunk = audio[i * CHUNK:(i + 1) * CHUNK]
        score = oww.predict(chunk)[name]
        print(f"{i}\t{score:.6f}")


if __name__ == "__main__":
    main()
