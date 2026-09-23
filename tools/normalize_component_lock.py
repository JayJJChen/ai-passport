"""Keep the local esp_xiaozhi override portable after IDF rewrites the lock."""

from pathlib import Path


root = Path(__file__).resolve().parent.parent
lock = root / "dependencies.lock"
component = root / "third_party" / "esp_xiaozhi"
source = lock.read_text(encoding="utf-8")
absolute = f"path: {component.as_posix()}"
portable = "path: third_party/esp_xiaozhi"
if absolute in source:
    lock.write_text(source.replace(absolute, portable), encoding="utf-8", newline="\n")
