import speech_recognition as sr
import webbrowser
import subprocess
import os
import re
import shutil
import threading
import tempfile
import wave
from pathlib import Path
from urllib.parse import quote_plus


VOICES_DIR = Path("voices")
_VOICE_FILES = {"Aria": "aria.onnx"}
_VOICE_PITCH = {"Aria": 1.2}
SPEED = 1.0
VOICE_RATE = 190
_piper_cache = {}
_HAVE_FFMPEG = shutil.which("ffmpeg") is not None


def list_voices():
    return [n for n in _VOICE_FILES if (VOICES_DIR / _VOICE_FILES[n]).exists()]


def _load_piper(name):
    if name not in _piper_cache:
        from piper import PiperVoice
        _piper_cache[name] = PiperVoice.load(str(VOICES_DIR / _VOICE_FILES[name]))
    return _piper_cache[name]


def set_voice(name):
    global _current_voice
    if name in list_voices():
        _current_voice = name


_current_voice = (list_voices() or [None])[0]
DEFAULT_VOICE = _current_voice


_mic_lock = threading.Lock()


_EMOJI = re.compile(
    "[\U0001F300-\U0001FAFF\U00002600-\U000027BF\U0001F900-\U0001F9FF"
    "\U0001F1E6-\U0001F1FF\U00002190-\U000021FF\U00002B00-\U00002BFF]",
    flags=re.UNICODE,
)


WAKE_WORDS = ("hey mimi", "konnichiwa mimi", "mimi san", "mimi")


recognizer = sr.Recognizer()


def listen(*, adjust: bool = True, timeout: float | None = None, phrase_time_limit: float = 8):
    with _mic_lock, sr.Microphone() as source:
        if adjust:
            print("Adjusting for background noise... please wait.")
            recognizer.adjust_for_ambient_noise(source, duration=1)
        print("Listening... speak now.")
        try:
            audio = recognizer.listen(source, timeout=timeout, phrase_time_limit=phrase_time_limit)
        except sr.WaitTimeoutError:
            return None

    print("Processing...")
    try:
        return recognizer.recognize_google(audio)
    except sr.UnknownValueError:
        print("Sorry, couldn't understand that.")
    except sr.RequestError:
        print("Couldn't connect to Google API. Check your internet.")
    return None


def has_wake_word(text: str | None):
    text = (text or "").lower()
    return any(word in text for word in WAKE_WORDS)


def get_url(spoken: str | None):
    if not spoken:
        return None

    words = spoken.lower()
    for phrase in WAKE_WORDS + ("open", "go to", "visit", "website", "dot com", "please"):
        words = words.replace(phrase, " ")

    name = "".join(words.split())
    if not name:
        return None
    if name.startswith("http"):
        return name
    return f"https://{name}.com"


def get_search_url(query):
    if not query:
        return None
    return "https://duckduckgo.com/?q=" + quote_plus(query)


def open_site(url: str | None):
    if url:
        print(f"Opening: {url}")
        webbrowser.open_new(url)
    return url


_audio_proc = None


def shush():
    global _audio_proc
    if _audio_proc and _audio_proc.poll() is None:
        _audio_proc.kill()
    _audio_proc = None


def _pitch_up(path, factor):
    if factor == 1.0 or not _HAVE_FFMPEG:
        return path
    with wave.open(path) as r:
        sr = r.getframerate()
    out = path[:-4] + "_p.wav"
    try:
        subprocess.run(
            ["ffmpeg", "-y", "-v", "error", "-i", path,
             "-af", f"asetrate={sr}*{factor},aresample={sr},atempo={1 / factor:.4f}", out],
            check=True)
        return out
    except Exception:
        return path


def _render_and_play(clean, voice):
    global _audio_proc
    name = voice or _current_voice
    if name in list_voices():
        try:
            from piper import SynthesisConfig
            fd, path = tempfile.mkstemp(suffix=".wav")
            os.close(fd)
            with wave.open(path, "wb") as wav:
                _load_piper(name).synthesize_wav(
                    clean, wav, syn_config=SynthesisConfig(length_scale=SPEED))
            play = _pitch_up(path, _VOICE_PITCH.get(name, 1.0))
            shush()
            _audio_proc = subprocess.Popen(["afplay", play])
            _audio_proc.wait()
            for p in {path, play}:
                os.unlink(p)
            return
        except Exception:
            pass
    shush()
    _audio_proc = subprocess.Popen(["say", "-r", str(VOICE_RATE), clean])
    _audio_proc.wait()


def speak(text, voice=None, wait=True):
    if not text:
        return text
    clean = " ".join(_EMOJI.sub("", text).split())
    if not clean:
        return text
    if wait:
        _render_and_play(clean, voice)
    else:
        threading.Thread(target=_render_and_play, args=(clean, voice), daemon=True).start()
    return text


if __name__ == "__main__":
    heard = listen()
    print(f"You said: {heard}")
    if has_wake_word(heard):
        open_site(get_url(heard))
    else:
        print("(no wake word — ignoring)")
