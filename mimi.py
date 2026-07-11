import json
import re


from speech import (
    listen, has_wake_word, get_url, get_search_url, open_site, speak, WAKE_WORDS,
)
from model import answer, chat
import journal
import tools


STOP_PHRASES = ("go to sleep", "goodbye mimi", "stop listening", "shut down")


_ORDINAL_WORDS = {"first": 1, "second": 2, "third": 3, "fourth": 4, "fifth": 5,
                  "sixth": 6, "seventh": 7, "eighth": 8}


def _ordinal(text):
    for word, n in _ORDINAL_WORDS.items():
        if word in text:
            return n
    m = re.search(r"\b(\d+)(?:st|nd|rd|th)?\b", text)
    return int(m.group(1)) if m else None


_last_search = None
_last_opened = None


INTENT_SYSTEM = """You turn one utterance from the user into a command for their \
Mac voice assistant. Pick the single best action:
open_site       they want a website opened; argument = the site name
search_web      they want a web search; argument = the query
launch_app      they want a Mac application launched; argument = the app name
summarize_page  they want the page/tab they're viewing summarized or saved
note_page       they want notes taken of the page they're viewing
clipboard       they want the clipboard summarized or read
screenshot      they want a screenshot
battery         they ask about battery
time            they ask the time or date
volume_up / volume_down
chat            anything else: questions, conversation, requests to explain


If in doubt, pick chat. argument is "" unless the action needs one."""


INTENT_SCHEMA = {
    "type": "object",
    "properties": {
        "action": {"type": "string", "enum": [
            "open_site", "search_web", "launch_app", "summarize_page", "note_page",
            "clipboard", "screenshot", "battery", "time",
            "volume_up", "volume_down", "chat"]},
        "argument": {"type": "string"},
    },
    "required": ["action", "argument"],
}


_CANON = {
    "open_site":      lambda arg: f"open {arg}",
    "search_web":     lambda arg: f"search {arg}",
    "launch_app":     lambda arg: f"launch {arg}",
    "summarize_page": lambda arg: "summarize this",
    "note_page":      lambda arg: "take notes of this",
    "clipboard":      lambda arg: "summarize my clipboard",
    "screenshot":     lambda arg: "screenshot",
    "battery":        lambda arg: "battery",
    "time":           lambda arg: "what time is it",
    "volume_up":      lambda arg: "volume up",
    "volume_down":    lambda arg: "volume down",
}


_QUESTION_STARTS = ("what", "why", "how", "who", "when", "where", "is ", "are ",
                    "explain", "tell me")


def _classify(command):
    try:
        d = json.loads(chat(INTENT_SYSTEM, command, schema=INTENT_SCHEMA, max_tokens=60))
        return d.get("action", "chat"), (d.get("argument") or "").strip()
    except Exception:
        return "chat", ""


def strip_wake(text):
    cleaned = text.lower()
    for word in WAKE_WORDS:
        cleaned = cleaned.replace(word, " ")
    return cleaned.strip()


def route(command):
    if not command:
        return "I didn't catch that."
    lower = command.lower()

    if ("save this page" in lower or "save that page" in lower or "capture this" in lower
            or "summarize this" in lower or "summarise this" in lower
            or "take notes" in lower or "note this" in lower):
        url = tools.current_tab_url() or _last_opened
        if not url:
            return "I can't see a browser tab right now."
        record = tools.capture(url)
        if "error" in record:
            return record["error"]
        if "note" in lower:
            tools.save_page_notes(record)
            return f"Noted. {record['title']} — summary and key points are in your notes."
        return f"{record['title']}: {record['summary']}"

    reminder = tools.parse_reminder(lower)
    if reminder:
        seconds, what = reminder
        tools.remind(seconds, what)
        mins = seconds // 60
        when = f"{mins} minute{'s' if mins != 1 else ''}" if mins else f"{seconds} seconds"
        return f"Okay, I'll remind you to {what} in {when}."

    if "clipboard" in lower and ("summarize" in lower or "summarise" in lower or "read" in lower):
        text = tools.clipboard()
        if not text:
            return "Your clipboard is empty."
        reply = chat("Summarize this in two spoken-friendly sentences, plain words, no markdown.",
                     text[:6000], max_tokens=120)
        journal.log("clipboard", {"summary": reply})
        return reply

    if "what time" in lower or "the time" in lower or "what's the date" in lower or "what day" in lower:
        return tools.what_time()

    if "battery" in lower:
        return tools.battery()

    if "screenshot" in lower or "screen shot" in lower:
        path = tools.screenshot()
        journal.log("screenshot", {"path": path})
        return "Done, it's on your desktop."

    if lower.startswith("find ") and ("file" in lower or "." in lower):
        name = lower.replace("find", "", 1).replace("my file", "").replace("file", "").strip()
        hits = tools.find_file(name)
        if not hits:
            return f"I couldn't find anything called {name}."
        open_site("file://" + hits[0])
        return f"Found {len(hits)}. Opening the best match: {hits[0].split('/')[-1]}"

    if "volume up" in lower or "louder" in lower:
        tools.volume(+15)
        return "Louder."
    if "volume down" in lower or "quieter" in lower:
        tools.volume(-15)
        return "Quieter."

    if lower.startswith("launch "):
        name = command[7:].strip()
        if tools.open_app(name):
            journal.log("app-open", {"app": name})
            return f"Launching {name}."
        return f"I couldn't find an app called {name}."

    if ("result" in lower or "link" in lower) and ("open" in lower or "go to" in lower):
        if not _last_search:
            return "Search for something first, then I can open a result."
        n = _ordinal(lower) or 1
        results = tools.web_results(_last_search)
        if len(results) < n:
            return f"I only found {len(results)} results for {_last_search}."
        title, url = results[n - 1]
        globals()["_last_opened"] = open_site(url)
        journal.log("page-open", {"url": url, "via": f"result {n}: {_last_search}"})
        return f"Opening result {n}: {title}."

    if lower.startswith("open ") or lower.startswith("go to "):
        url = open_site(get_url(command))
        if url:
            globals()["_last_opened"] = url
            journal.log("page-open", {"url": url})

            name = url.split("//", 1)[-1].removesuffix(".com").strip("/")
            return f"Opening {name}."
        return "I couldn't work out which site."

    if lower.startswith("search "):
        query = command.replace("search", "", 1).strip()
        globals()["_last_search"] = query
        open_site(get_search_url(query))
        journal.log("search", {"query": query})
        return f"Searching for {query}. Say 'open the first result' if you want it."

    if not lower.startswith(_QUESTION_STARTS):
        action, arg = _classify(command)
        if action in _CANON:
            return route(_CANON[action](arg))

    reply = answer(command)
    journal.log("chat", {"question": command, "answer": reply})
    return reply


def handle(command):
    speak(route(command))


def main():
    speak("Mimi is awake and listening.")
    first = True
    while True:
        heard = listen(adjust=first)
        first = False
        if not heard:
            continue
        print("heard:", heard)

        if not has_wake_word(heard):
            continue

        if any(phrase in heard.lower() for phrase in STOP_PHRASES):
            speak("Going to sleep. Bye.")
            break

        handle(strip_wake(heard))


if __name__ == "__main__":
    main()
