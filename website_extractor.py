import re
from pathlib import Path
from urllib.parse import urlparse
import requests
from bs4 import BeautifulSoup



NOT_IMPORTANT = [
    "script", "style", "noscript", "img", "svg", "picture", "video", "audio",
    "iframe", "canvas", "form", "button", "input", "footer", "header", "nav",
    "aside",
]



def extract_text(url: str, *, timeout: float = 15):
    response = requests.get(url, headers={"User-Agent": "Mozilla/5.0"}, timeout=timeout)
    response.raise_for_status()

    soup = BeautifulSoup(response.text, "html.parser")
    for tag in soup(NOT_IMPORTANT):
        tag.decompose()

    text = soup.get_text(separator="\n")
    lines = [line.strip() for line in text.splitlines() if line.strip()]
    return "\n".join(lines)


def _slug(url: str):
    parsed = urlparse(url)
    raw = (parsed.netloc + parsed.path) or url
    return re.sub(r"[^a-zA-Z0-9]+", "_", raw).strip("_") or "page"


def save_text(text: str, url: str = "", folder: str = "pages"):
    folder_path = Path(folder)
    folder_path.mkdir(parents=True, exist_ok=True)
    path = folder_path / f"{_slug(url) if url else 'page'}.txt"
    path.write_text(text, encoding="utf-8")
    return path



if __name__ == "__main__":
    url = input("Enter URL: ")
    text = extract_text(url)
    print(text)
    saved = save_text(text, url)
    print(f"\nSaved to {saved}")