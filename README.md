# Mimi ミミ

A private, fully-offline voice assistant for macOS. Talk to her, and she acts on
your Mac — opens sites, searches the web, summarizes the page you're reading,
takes meeting notes, sets reminders, and more. In the background she quietly
learns what you care about and writes you a daily reflection about it.

**Nothing ever leaves your machine.** The language model, the speech recognition,
the text-to-speech, and the memory all run locally.

## What she can do

**Talk to her** — she's always listening for "hey mimi", or type in the app. She
replies in a natural neural voice and remembers the conversation.

**Act on your Mac**

| You say… | She… |
|---|---|
| "summarize this" / "take notes of this" | reads the browser tab you're on |
| "search rust tutorials" → "open the second result" | opens the exact result |
| "open youtube" / "launch spotify" | opens a site / launches an app |
| "remind me in 20 minutes to stretch" | notification + spoken reminder |
| "summarize my clipboard" | summarizes what you copied |
| "what time is it" / "how's my battery" / "take a screenshot" | does it |
| "find my file model.py" | Spotlight search |
| anything else | answers as an assistant |

**Learns you** — every couple of minutes she notes the page you're on (locally),
and the daily reflection turns that into a note about what you were into, plus an
evolving profile she uses to personalize her answers. All viewable in the "For me"
tab. Nothing is sent anywhere.

## Stack

- **LLM** — [Ollama](https://ollama.com) running `gemma3n:e4b` (chat) and
  `nomic-embed-text` (embeddings)
- **Speech-to-text** — `SpeechRecognition`
- **Text-to-speech** — [Piper](https://github.com/rhasspy/piper) neural voice, local
- **App** — a native macOS window via `pywebview` (no browser, no server)

## Architecture

```
speech.py   voice in (mic) + out (Piper neural TTS)
model.py    the Ollama brain: chat, extract, embed, reflect
tools.py    real Mac actions (AppleScript, Spotlight, clipboard, …)
mimi.py     route(): one place that turns an utterance into an action
journal.py  a local log of what you do each day
reflect.py  end-of-day reflection → digest + evolving profile
app.py      the desktop app: native window + always-listening loop + login
```

Both the voice loop and the app window go through the same `route()` in `mimi.py`,
so typing and talking always behave identically.

## Setup

Requires macOS, Python 3.12, [Ollama](https://ollama.com), and `ffmpeg`
(`brew install ffmpeg`).

```bash
python3 -m venv venv
source venv/bin/activate
pip install -r requirements.txt

ollama pull gemma3n:e4b
ollama pull nomic-embed-text
```

Download the neural voice (not committed — it's ~60 MB):

```bash
mkdir -p voices
curl -L -o voices/aria.onnx \
  https://huggingface.co/rhasspy/piper-voices/resolve/main/en/en_US/hfc_female/medium/en_US-hfc_female-medium.onnx
curl -L -o voices/aria.onnx.json \
  https://huggingface.co/rhasspy/piper-voices/resolve/main/en/en_US/hfc_female/medium/en_US-hfc_female-medium.onnx.json
```

## Run

```bash
ollama serve          # if it isn't already running
python app.py
```

First launch asks you to pick a name and password (stored as a salted SHA-256
hash — login is required every time). macOS will ask once for Microphone and
Automation permission; allow both.
