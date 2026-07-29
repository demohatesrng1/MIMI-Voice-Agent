# Mimi ミミ

A private voice assistant for macOS, written in C++. She listens for
「ねえミミ」, answers in Japanese, and acts on your Mac — opens sites, launches
apps, controls playback, takes screenshots, sets reminders, reads the page
you're on.

**Nothing leaves the machine.** Speech recognition, the language model, speech
synthesis and the journal all run locally.

## Why C++

The original was Python. The rewrite is not for its own sake: **whisper.cpp runs
the whisper encoder on the Metal GPU**, which CTranslate2 (faster-whisper)
cannot. That is 13–15× realtime transcription on Apple Silicon, against roughly
1–1.5 s per utterance on CPU.

## How the listening works

```
mic ─▶ ring buffer (30s, lock-free, written from the CoreAudio realtime thread)
         ├─▶ Silero VAD        32 ms windows, endpointing with hysteresis
         ├─▶ wake detection    openWakeWord, or a whisper phrase spotter
         └─▶ pre-roll          500 ms of lookback
                    │
                    ▼  utterance captured
              whisper.cpp (Metal) ─▶ router ─▶ Ollama / Mac controls ─▶ speech
```

The microphone is opened once and never released mid-conversation, so there is
no window where she is deaf and no lock for a push-to-talk request to contend
with. Transcription runs on a second thread, so the audio loop keeps consuming
frames while she is thinking.

Details that matter and are easy to get wrong:

- **Silero VAD v5 wants 576 samples**, not 512 — 64 samples of carried context
  plus 512 new. Fed only 512 it returns ≈0 forever and never errors.
- **openWakeWord wants int16-scaled audio** (±32768, not ±1.0), a `mel/10 + 2`
  transform, and a `ones((76,32))` seed buffer.
- Both are checked numerically against the Python reference rather than trusted:
  `scripts/reference_scores.py` + `mimi_wake --compare` agree to **4.8e-07**.

## Wake word

openWakeWord ships no `hey mimi` model, and its pretrained classifiers are tied
to the acoustics of their own phrase. So there are two backends:

| Backend | Wake phrase | Trade-off |
|---|---|---|
| `PhraseSpotter` (default) | anything | Works today. Transcribes every speech segment to find the phrase. |
| `OpenWakeWord` | needs a trained `.onnx` | Scores audio acoustically, transcribes nothing until it fires. |

Japanese makes the spotter interesting: whisper writes ミミ as **耳** (kanji for
"ear", identical pronunciation) far more often than as katakana, so the matcher
folds homophones — and rejects 「耳が痛い」 by testing for a following case
particle.

## Build

Requires macOS, CMake ≥ 3.24 and Xcode Command Line Tools.

```bash
brew install cmake ninja onnxruntime qt nlohmann-json
./scripts/fetch_models.sh          # ~600 MB, gitignored
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH="$(brew --prefix)"
cmake --build build
open build/Mimi.app
```

For the brain: [Ollama](https://ollama.com) with `ollama pull gemma3n:e4b` and
`ollama pull nomic-embed-text`, then `ollama serve`.

macOS will ask once for **Microphone** and once for **Automation**. Both are
required; without the second, controlling other apps fails with error `-1743`.

## Layout

```
src/audio/    ring buffer, CoreAudio capture, WAV
src/voice/    VAD, wake word, phrase spotter, whisper, TTS, listener
src/brain/    Ollama client, Mac controls, router, journal, reflection
src/ui/       Qt6 window, voice orb, chat view, theme
src/app/      main
tools/        one CLI per layer, for testing without the GUI
```

## Command-line harnesses

Each layer is testable on its own, which is how the bugs above were found:

```bash
./build/mimi_mic                 # live level meter, checks capture
./build/mimi_wake --live         # wake-word scores in realtime
./build/mimi_wake FILE --compare TSV   # diff against openWakeWord
./build/mimi_listen --vad FILE   # Silero probabilities + endpoint decisions
./build/mimi_listen              # full pipeline, no GUI
./build/mimi_spot                # wake-phrase matcher test suite
./build/mimi_tools               # probe every Mac control (read-only)
./build/mimi_route "今何時ですか" # drive the router, no microphone
./build/mimi_brain_cli --check   # Ollama reachable? which models?
```

## Safety

Mimi's arguments come from a model interpreting speech, so:

- Every subprocess runs through `posix_spawnp` with an **argv array, never a
  shell**. There is no string to inject into.
- Anything embedded in AppleScript is escaped, since that *is* still a language
  being assembled from text.
- Deleting files, emptying the trash, shutting down and keychain access are not
  offered at all.

## Privacy

Audio is held in a ring buffer and never written to disk. The journal, digests
and profile live in `~/Library/Application Support/Mimi/` and are gitignored.

The `PhraseSpotter` backend does transcribe every speech segment locally in
order to find the wake phrase. If you want audio only processed *after* the
wake word, use the `OpenWakeWord` backend with a trained model.
