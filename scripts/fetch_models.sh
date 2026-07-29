#!/usr/bin/env bash
# Fetch every model Mimi's voice pipeline needs. Idempotent -- re-running only
# downloads what's missing. Models are gitignored; they total ~500 MB.
set -euo pipefail

MODELS_DIR="${MIMI_MODELS_DIR:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/models}"
mkdir -p "$MODELS_DIR"

OWW_RELEASE="https://github.com/dscripka/openWakeWord/releases/download/v0.5.1"
SILERO_RAW="https://raw.githubusercontent.com/snakers4/silero-vad/master/src/silero_vad/data"
WHISPER_HF="https://huggingface.co/ggerganov/whisper.cpp/resolve/main"

WHISPER_MODEL="${MIMI_WHISPER_MODEL:-ggml-small.en.bin}"

fetch() {
  local url="$1" dest="$2" label="$3"
  if [[ -s "$MODELS_DIR/$dest" ]]; then
    printf '  ok    %-34s (have it)\n' "$label"
    return 0
  fi
  printf '  get   %-34s' "$label"
  if curl -fsSL --retry 3 --retry-delay 2 -o "$MODELS_DIR/$dest.partial" "$url"; then
    mv "$MODELS_DIR/$dest.partial" "$MODELS_DIR/$dest"
    printf 'done (%s)\n' "$(du -h "$MODELS_DIR/$dest" | cut -f1 | tr -d ' ')"
  else
    rm -f "$MODELS_DIR/$dest.partial"
    printf 'FAILED\n'
    return 1
  fi
}

echo "models -> $MODELS_DIR"
echo
echo "voice activity detection"
fetch "$SILERO_RAW/silero_vad.onnx" "silero_vad.onnx" "Silero VAD v5"

echo
echo "wake word (openWakeWord shared frontend)"
fetch "$OWW_RELEASE/melspectrogram.onnx"  "melspectrogram.onnx"  "mel spectrogram"
fetch "$OWW_RELEASE/embedding_model.onnx" "embedding_model.onnx" "speech embedding"

echo
echo "wake word (pretrained phrases)"
fetch "$OWW_RELEASE/hey_jarvis_v0.1.onnx"  "hey_jarvis_v0.1.onnx"  "hey jarvis"
fetch "$OWW_RELEASE/alexa_v0.1.onnx"       "alexa_v0.1.onnx"       "alexa"
fetch "$OWW_RELEASE/hey_mycroft_v0.1.onnx" "hey_mycroft_v0.1.onnx" "hey mycroft"

echo
echo "speech to text"
fetch "$WHISPER_HF/$WHISPER_MODEL" "$WHISPER_MODEL" "whisper ${WHISPER_MODEL}"

echo
echo "done."
echo
echo "Note: openWakeWord ships no 'hey mimi' model. Mimi defaults to 'hey jarvis'"
echo "until you train one -- see docs/wake-word.md. Point MIMI_WAKE_MODEL at a"
echo "custom .onnx to use it."
