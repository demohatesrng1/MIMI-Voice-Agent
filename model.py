import os
import json
import ollama


CHAT_MODEL = os.environ.get("ASSISTANT_MODEL", "gemma3n:e4b")


EMBED_MODEL = os.environ.get("ASSISTANT_EMBED_MODEL", "nomic-embed-text")


_client = ollama.Client(host=os.environ.get("OLLAMA_HOST", "http://localhost:11434"))


KEEP_ALIVE = os.environ.get("ASSISTANT_KEEP_ALIVE", "1h")


EXTRACT_SYSTEM = """You are the perception layer of a personal assistant that \
learns what its user cares about. You are given the text of a web page or a \
spoken transcript the user just engaged with.


Report only what is actually present in the text. Be concise and concrete. Do \
not invent facts, quotes, or numbers. If a field has no support in the text, \
return an empty string or empty list.


For 'inferred_interest', and only there, you may reason: in one sentence, why \
would this user have opened or discussed this? Base it on what they seem to be \
seeking (learning, deciding, buying, debugging, curiosity...). This single \
guess is the assistant's memory of the user's intent, so make it specific.


Write every field in English, even if the source text is in another language."""


ANSWER_SYSTEM = """You are Mimi, the user's personal voice assistant. Your \
replies are read out loud, so talk like a calm, natural person: plain everyday \
words, contractions, usually one to three short sentences. Be warm and direct, \
NOT bubbly or gushy — no "cool, right?", no "I just knew that!". Never use \
emojis, markdown, bullet points or headings; none of that works when spoken.


You may be given CONTEXT: snippets from things the user actually read or said \
before. If CONTEXT is present and relevant, use it and you may say where it came \
from. If there is NO CONTEXT, just answer from your own knowledge, and never \
pretend the user read, saw, or told you something — do not invent a shared \
history that isn't in the CONTEXT."""


NOTES_SYSTEM = """You produce minutes of meetings from transcripts: a short \
summary, key discussion points, decisions, and action items with owners. Use \
markdown, no code blocks. Only include what the transcript supports."""


REFLECT_SYSTEM = """You are Mimi, writing the user a short daily note about their \
day. You are given a log of what they did — pages they read, things they searched, \
questions they asked. Write a warm, personal 3-6 sentence message, first-person, \
addressed to "you".


Notice patterns and what they seemed to care about or be working toward. Be \
specific about the actual topics, not generic. Point out one thread worth \
following up on if you see one. Do not just list events — reflect on them. Only \
use what's in the log; never invent activity. No markdown, no bullet points."""


PROFILE_SYSTEM = """You maintain a short evolving profile of the user — who they \
are and what they care about — based on their existing profile plus today's note. \
Rewrite the profile so it stays under ~150 words: keep durable interests and \
traits, fold in anything new and meaningful, drop one-off noise. Write it as \
plain sentences about the user. No markdown headings, no lists."""


PAGE_SCHEMA = {
    "type": "object",
    "properties": {
        "title": {"type": "string"},
        "summary": {"type": "string", "description": "2-3 sentence gist"},
        "category": {"type": "string", "description": "e.g. programming, finance, health"},
        "topics": {"type": "array", "items": {"type": "string"},
                   "description": "3-8 short tags for interest tracking"},
        "key_points": {"type": "array", "items": {"type": "string"}},
        "entities": {"type": "array", "items": {"type": "string"},
                     "description": "people, orgs, products, tools mentioned"},
        "inferred_interest": {"type": "string",
                              "description": "one sentence: why the user likely cared"},
    },
    "required": ["title", "summary", "category", "topics", "inferred_interest"],
}


def _chat_stream(kwargs):
    for part in _client.chat(**kwargs, stream=True):
        yield part["message"]["content"]


def chat(system: str, user: str, *, stream: bool = False, schema: dict | None = None,
         max_tokens: int | None = None, history: list | None = None):
    messages = [{"role": "system", "content": system}]
    messages += history or []
    messages.append({"role": "user", "content": user})
    kwargs = {"model": CHAT_MODEL, "messages": messages, "keep_alive": KEEP_ALIVE}
    options = {}
    if schema is not None:
        kwargs["format"] = schema
        options["temperature"] = 0
    if max_tokens:
        options["num_predict"] = max_tokens
    if options:
        kwargs["options"] = options

    if stream:
        return _chat_stream(kwargs)

    resp = _client.chat(**kwargs)

    return resp["message"]["content"].replace("▁", " ").strip()


def warmup():
    try:
        _client.chat(model=CHAT_MODEL, messages=[{"role": "user", "content": "hi"}],
                     options={"num_predict": 1}, keep_alive=KEEP_ALIVE)
    except Exception:
        pass


def extract_page(text: str, url: str = ""):
    user = f"Source URL: {url or 'n/a'}\n\nText to analyze:\n{text}"
    raw = chat(EXTRACT_SYSTEM, user, schema=PAGE_SCHEMA)
    data = json.loads(raw)
    data["url"] = url
    return data


def embed(text: str):
    return _client.embeddings(model=EMBED_MODEL, prompt=text)["embedding"]


def _profile():
    try:
        return open("profile.md", encoding="utf-8").read().strip()
    except FileNotFoundError:
        return ""


_history = []
HISTORY_TURNS = 12


def answer(question: str, context_chunks: list[str] | None = None, *, stream: bool = False):
    system = ANSWER_SYSTEM
    profile = _profile()
    if profile:
        system += f"\n\nWhat you know about the user so far:\n{profile}"
    if context_chunks:
        context = "\n\n---\n".join(context_chunks)
        user = f"CONTEXT:\n{context}\n\nQUESTION:\n{question}"
    else:
        user = question

    reply = chat(system, user, stream=stream, max_tokens=220, history=list(_history))
    if not stream:
        _history.append({"role": "user", "content": question})
        _history.append({"role": "assistant", "content": reply})
        del _history[:-HISTORY_TURNS]
    return reply


def meeting_notes(transcript: str):
    return chat(NOTES_SYSTEM, f"Transcript:\n{transcript[:12000]}", max_tokens=800)


def daily_digest(events):
    if not events:
        return "Quiet day — I didn't see you get up to anything worth noting."
    lines = []
    for e in events:
        rec = e.get("record", {})

        gist = rec.get("summary") or rec.get("inferred_interest") or rec.get("query") \
            or rec.get("question") or rec.get("title") or rec.get("text") or str(rec)
        lines.append(f"- [{e.get('kind', '?')}] {gist}")
    return chat(REFLECT_SYSTEM, "Today's log:\n" + "\n".join(lines))


def update_profile(old_profile, todays_note):
    user = f"Existing profile:\n{old_profile or '(none yet)'}\n\nToday's note:\n{todays_note}"
    return chat(PROFILE_SYSTEM, user)


if __name__ == "__main__":
    sample = ("Ollama lets you run open models like Gemma locally. It exposes an "
              "HTTP API on port 11434 and a Python client. Good for private, "
              "offline AI apps.")
    record = extract_page(sample, url="https://ollama.com")
    print(json.dumps(record, indent=2))
    print("\nembedding length:", len(embed(sample)))
    print("\nspoken answer:", answer("What port does Ollama use?", [sample]))
