from datetime import date
from pathlib import Path

import journal
from model import daily_digest, update_profile

DIGEST_DIR = Path("digests")
PROFILE_FILE = Path("profile.md")
CONFIG_FILE = Path("config.json")


def run():
    events = journal.read_all()
    if not events:
        return None
    note = daily_digest(events)
    DIGEST_DIR.mkdir(parents=True, exist_ok=True)
    (DIGEST_DIR / f"{date.today().isoformat()}.md").write_text(note, encoding="utf-8")
    old_profile = PROFILE_FILE.read_text(encoding="utf-8") if PROFILE_FILE.exists() else ""
    PROFILE_FILE.write_text(update_profile(old_profile, note), encoding="utf-8")
    journal.wipe()
    return note


if __name__ == "__main__":
    print(run() or "Nothing in the journal yet.")
