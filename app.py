import hashlib
import json
import secrets
import threading
import time
from datetime import datetime
from pathlib import Path


import webview


import journal
import mimi
import model
import reflect
import speech
import tools
from model import meeting_notes
from speech import has_wake_word, listen, shush, speak


NOTES_DIR = Path("notes")


def _cfg():
    if reflect.CONFIG_FILE.exists():
        return json.loads(reflect.CONFIG_FILE.read_text(encoding="utf-8"))
    return {}


def _save_cfg(cfg):
    reflect.CONFIG_FILE.write_text(json.dumps(cfg, indent=2), encoding="utf-8")


def _hash_pw(password, salt):
    return hashlib.sha256((salt + password).encode()).hexdigest()


_window = None
_awake = threading.Event()
_awake.set()
_unlocked = threading.Event()
_speak_on = threading.Event()
_speak_on.set()


def _push(cls, text):
    if _window:
        _window.evaluate_js(f"pushMsg({json.dumps(cls)}, {json.dumps(text)})")


def _wake_loop():
    model.warmup()
    first = True
    while True:
        if not (_awake.is_set() and _unlocked.is_set()):
            time.sleep(0.4)
            continue

        heard = listen(adjust=first, timeout=5)
        first = False
        if not heard or not has_wake_word(heard):
            continue
        shush()
        command = mimi.strip_wake(heard)
        _push("you", heard)
        reply = mimi.route(command)
        _push("mimi", reply)
        if _speak_on.is_set():
            speak(reply, wait=False)


def _observer_loop():
    seen = set()
    while True:
        time.sleep(120)
        if not (_awake.is_set() and _unlocked.is_set()):
            continue
        url, title = tools.current_tab()
        if not url:
            continue
        key = url.split("#")[0].split("?")[0]
        if key in seen:
            continue
        seen.add(key)
        journal.log("browse", {"url": url, "title": title or url})


def _on_shown(window):
    global _window
    _window = window
    cfg = _cfg()
    speech.set_voice(cfg.get("voice", speech.DEFAULT_VOICE))
    if not cfg.get("speak", True):
        _speak_on.clear()

    threading.Thread(target=_wake_loop, daemon=True).start()
    threading.Thread(target=_observer_loop, daemon=True).start()


class Api:
    def ask(self, data):
        question = (data.get("question") or "").strip()
        if not question:
            return {"error": "empty question"}
        reply = mimi.route(question)
        if data.get("voice") and _speak_on.is_set():
            threading.Thread(target=speak, args=(reply,), daemon=True).start()
        return {"reply": reply}

    def listen(self):
        shush()
        heard = listen()
        return {"text": heard or ""}

    def capture(self, url):
        url = (url or "").strip()
        if not url:
            url = tools.current_tab_url()
            if not url:
                return {"error": "no url given, and I can't see a browser tab"}
        if not url.startswith("http"):
            url = "https://" + url
        return tools.capture(url)

    def set_awake(self, on):
        if on:
            _awake.set()
        else:
            _awake.clear()
        return {"awake": _awake.is_set()}

    def set_speak(self, on):
        if on:
            _speak_on.set()
        else:
            _speak_on.clear()
            shush()
        cfg = _cfg()
        cfg["speak"] = bool(on)
        _save_cfg(cfg)
        return {"speak": _speak_on.is_set()}

    def voices(self):
        return speech.list_voices()

    def set_voice(self, name):
        speech.set_voice(name)
        cfg = _cfg()
        cfg["voice"] = name
        _save_cfg(cfg)
        if _speak_on.is_set():
            threading.Thread(target=speak, args=(f"Hi, this is {name}.",),
                             daemon=True).start()
        return {"voice": name}

    def notes(self, transcript):
        transcript = (transcript or "").strip()
        if not transcript:
            return {"error": "empty transcript"}
        minutes = meeting_notes(transcript)
        NOTES_DIR.mkdir(exist_ok=True)
        stamp = datetime.now().strftime("%Y-%m-%d_%H-%M")
        path = NOTES_DIR / f"meeting_{stamp}.md"
        path.write_text(minutes, encoding="utf-8")
        journal.log("meeting", {"summary": minutes[:300], "file": str(path)})
        return {"minutes": minutes, "file": str(path)}

    def list_notes(self):
        if not NOTES_DIR.exists():
            return []
        return [{"name": p.stem, "text": p.read_text(encoding="utf-8")}
                for p in sorted(NOTES_DIR.glob("*.md"), reverse=True)]

    def forme(self):
        profile = reflect.PROFILE_FILE.read_text(encoding="utf-8") \
            if reflect.PROFILE_FILE.exists() else ""
        digests = []
        if reflect.DIGEST_DIR.exists():
            for p in sorted(reflect.DIGEST_DIR.glob("*.md"), reverse=True)[:14]:
                digests.append({"date": p.stem, "text": p.read_text(encoding="utf-8")})
        return {"profile": profile, "digests": digests,
                "today_events": len(journal.read_day())}

    def register(self, data):
        password = (data.get("password") or "").strip()
        if not password:
            return {"error": "password can't be empty"}
        cfg = _cfg()
        salt = secrets.token_hex(8)
        cfg["user"] = {
            "name": (data.get("name") or "").strip() or "you",
            "salt": salt,
            "hash": _hash_pw(password, salt),
        }
        _save_cfg(cfg)
        _unlocked.set()
        return {"ok": True, "name": cfg["user"]["name"]}

    def login(self, data):
        cfg = _cfg()
        user = cfg.get("user")
        if not user:
            return {"error": "no account yet"}
        if _hash_pw((data.get("password") or ""), user["salt"]) != user["hash"]:
            return {"ok": False}
        _unlocked.set()
        return {"ok": True, "name": user["name"]}

    def get_settings(self):
        cfg = _cfg()
        user = cfg.get("user")
        return {"awake": _awake.is_set(), "speak": _speak_on.is_set(),
                "voice": cfg.get("voice", speech.DEFAULT_VOICE),
                "user": user["name"] if user else ""}

    def reflect_now(self):
        note = reflect.run()
        return {"note": note or ""}


if __name__ == "__main__":
    win = webview.create_window(
        "Mimi ミミ", "static/index.html", js_api=Api(),
        width=1120, height=740, min_size=(880, 600),
        background_color="#0b0e1a",
    )
    webview.start(_on_shown, win)
