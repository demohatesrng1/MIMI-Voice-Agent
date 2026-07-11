import re
import subprocess
import threading
from datetime import datetime
from pathlib import Path
from urllib.parse import urlparse, parse_qs, unquote


import requests
from bs4 import BeautifulSoup


import journal
from model import extract_page
from website_extractor import extract_text, save_text


def _osa(script):
    r = subprocess.run(["osascript", "-e", script], capture_output=True, text=True)
    return r.stdout.strip()


_BROWSERS = [
    ("Google Chrome", 'tell application "Google Chrome" to get URL of active tab of front window'),
    ("Arc",           'tell application "Arc" to get URL of active tab of front window'),
    ("Safari",        'tell application "Safari" to get URL of front document'),
]


def current_tab_url():
    for app, script in _BROWSERS:
        if _osa(f'application "{app}" is running') == "true":
            url = _osa(script)
            if url.startswith("http"):
                return url
    return None


_TITLES = {
    "Google Chrome": 'tell application "Google Chrome" to get title of active tab of front window',
    "Arc":           'tell application "Arc" to get title of active tab of front window',
    "Safari":        'tell application "Safari" to get name of front document',
}


def current_tab():
    for app, script in _BROWSERS:
        if _osa(f'application "{app}" is running') == "true":
            url = _osa(script)
            if url.startswith("http"):
                return url, _osa(_TITLES[app])
    return None, None


def web_results(query, limit=8):
    r = requests.post("https://html.duckduckgo.com/html/", data={"q": query},
                      headers={"User-Agent": "Mozilla/5.0"}, timeout=15)
    soup = BeautifulSoup(r.text, "html.parser")
    results = []
    for a in soup.select("a.result__a"):
        href = a.get("href", "")
        if "uddg=" in href:
            href = unquote(parse_qs(urlparse(href).query).get("uddg", [""])[0])
        if href.startswith("http"):
            results.append((a.get_text(strip=True), href))
        if len(results) >= limit:
            break
    return results


def capture(url):
    try:
        text = extract_text(url)
    except Exception as e:
        return {"error": f"couldn't fetch that page: {e}"}
    save_text(text, url)
    record = extract_page(text, url)
    journal.log("page", record)
    return record


_UNITS = {"second": 1, "minute": 60, "hour": 3600}


def parse_reminder(command):
    m = re.search(r"remind me (?:in (\d+) (second|minute|hour)s?)?\s*(?:to )?(.+?)"
                  r"(?: in (\d+) (second|minute|hour)s?)?$", command)
    if not m or "remind me" not in command:
        return None
    num, unit = (m.group(1), m.group(2)) if m.group(1) else (m.group(4), m.group(5))
    if not num:
        return None
    what = m.group(3).strip()
    return int(num) * _UNITS[unit], what


def notify(title, text):
    _osa(f'display notification "{text}" with title "{title}"')


def remind(seconds, text):
    from speech import speak

    def fire():
        notify("Mimi", text)
        speak(f"Hey, reminding you: {text}")

    threading.Timer(seconds, fire).start()
    journal.log("reminder", {"in_seconds": seconds, "text": text})


def clipboard():
    r = subprocess.run(["pbpaste"], capture_output=True, text=True)
    return r.stdout.strip()


def what_time():
    now = datetime.now()
    return now.strftime("It's %-I:%M %p on %A, %B %-d.")


def volume(step):
    _osa(f"set volume output volume ((output volume of (get volume settings)) + {step})")


def open_app(name):
    r = subprocess.run(["open", "-a", name], capture_output=True)
    return r.returncode == 0


def battery():
    out = subprocess.run(["pmset", "-g", "batt"], capture_output=True, text=True).stdout
    m = re.search(r"(\d+)%; (\w+)", out)
    if not m:
        return "I couldn't read the battery."
    pct, state = m.group(1), m.group(2)
    state = {"discharging": "on battery", "charging": "charging",
             "charged": "fully charged"}.get(state, state)
    return f"Battery is at {pct} percent, {state}."


def screenshot():
    path = Path.home() / "Desktop" / f"mimi_shot_{datetime.now().strftime('%H-%M-%S')}.png"
    subprocess.run(["screencapture", "-x", str(path)])
    return str(path)


def find_file(name, limit=5):
    r = subprocess.run(["mdfind", "-name", name], capture_output=True, text=True)
    hits = [line for line in r.stdout.splitlines() if line.strip()]
    return hits[:limit]


def save_page_notes(record):
    notes_dir = Path("notes")
    notes_dir.mkdir(exist_ok=True)
    stamp = datetime.now().strftime("%Y-%m-%d_%H-%M")
    path = notes_dir / f"page_{stamp}.md"
    points = "\n".join(f"- {p}" for p in record.get("key_points", [])) or "- (no key points found)"
    path.write_text(
        f"# {record.get('title', 'Page')}\n\n{record.get('url', '')}\n\n"
        f"{record.get('summary', '')}\n\n## Key points\n{points}\n",
        encoding="utf-8",
    )
    return str(path)
