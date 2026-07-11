import json
from datetime import date, datetime
from pathlib import Path


JOURNAL_DIR = Path("journal")


def _day_file(day=None):
    day = day or date.today().isoformat()
    return JOURNAL_DIR / f"{day}.jsonl"


def log(kind, record):
    JOURNAL_DIR.mkdir(parents=True, exist_ok=True)
    event = {
        "time": datetime.now().isoformat(timespec="seconds"),
        "kind": kind,
        "record": record,
    }
    with _day_file().open("a", encoding="utf-8") as f:
        f.write(json.dumps(event, ensure_ascii=False) + "\n")
    return event


def read_day(day=None):
    path = _day_file(day)
    if not path.exists():
        return []
    events = []
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if line:
            events.append(json.loads(line))
    return events


def read_all():
    if not JOURNAL_DIR.exists():
        return []
    events = []
    for path in sorted(JOURNAL_DIR.glob("*.jsonl")):
        for line in path.read_text(encoding="utf-8").splitlines():
            line = line.strip()
            if line:
                events.append(json.loads(line))
    return events


def wipe():
    if not JOURNAL_DIR.exists():
        return 0
    count = 0
    for path in JOURNAL_DIR.glob("*.jsonl"):
        path.unlink()
        count += 1
    return count
