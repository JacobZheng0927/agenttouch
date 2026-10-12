"""Read-only Codex Desktop turn lifecycle; no transcript replay on startup."""
import datetime
import json
import re
import sqlite3
import subprocess
import time
from pathlib import Path

EVENTS = {"task_started", "task_complete", "turn_aborted"}


def desktop_backend_start(now):
    rows = subprocess.check_output(["ps", "-axo", "etime=,command="], text=True)
    starts = []
    for row in rows.splitlines():
        parts = row.strip().split(None, 1)
        if len(parts) != 2 or not re.search(r"/(?:ChatGPT|Codex)\.app/.*codex(?: app-server|.*? app-server)", parts[1]):
            continue
        elapsed = parts[0]
        days, elapsed = elapsed.split("-", 1) if "-" in elapsed else ("0", elapsed)
        seconds = 0
        for part in elapsed.split(":"):
            seconds = seconds * 60 + int(part)
        starts.append(now - int(days) * 86400 - seconds)
    return min(starts) if starts else None


def event(line):
    if not any(('"' + kind + '"').encode() in line for kind in EVENTS):
        return None
    try:
        data = json.loads(line)
        p = data.get("payload", {})
        if data.get("type") != "event_msg" or p.get("type") not in EVENTS:
            return None
        attribution = p.get("turn_attribution") or {}
        if attribution.get("initiating_agent_path"):
            return None
        stamp = datetime.datetime.fromisoformat(data["timestamp"].replace("Z", "+00:00")).timestamp()
        return stamp, p
    except (ValueError, KeyError, TypeError, AttributeError):
        return None


def latest_event(path, end):
    with path.open("rb") as f:
        pos, carry = end, b""
        while pos:
            start = max(0, pos - 131072)
            f.seek(start)
            lines = (f.read(pos - start) + carry).split(b"\n")
            carry = lines.pop(0) if start else b""
            for line in reversed(lines):
                result = event(line)
                if result:
                    return result
            pos = start
    return None


def clipped(text, limit):
    text = str(text or "").replace("\r", "")
    text = re.sub(r"[\x00-\x08\x0b\x0c\x0e-\x1f]", "", text)
    raw = text.encode("utf-8")
    return text if len(raw) <= limit else raw[:limit - 3].decode("utf-8", "ignore") + "…"


def completion(sid, title, stamp, payload):
    text = payload.get("last_agent_message") or "本轮回复已结束，暂无文本内容。"
    text = re.sub(r"<oai-mem-citation>.*?</oai-mem-citation>", "", text, flags=re.S)
    text = re.sub(r"\[([^\]]+)\]\([^)]*\)", r"\1", text)
    text = re.sub(r"(?m)^#{1,6}\s+|^```[^\n]*|\*\*", "", text).strip()
    local = datetime.datetime.fromtimestamp(stamp, datetime.timezone(datetime.timedelta(hours=8)))
    seconds = max(0, int(payload.get("duration_ms", 0) / 1000))
    return {"id": sid + ":" + str(payload.get("turn_id") or payload.get("root_turn_id") or stamp),
            "title": clipped(title or "Codex", 144), "body": clipped(text, 600),
            "meta": local.strftime("%Y%m%d %H:%M") + " · %dm%02ds" % divmod(seconds, 60)}


class CodexActivity:
    def __init__(self, root=None):
        self.root = Path(root) if root else Path.home() / ".codex"
        self.started = time.time()
        self.files = {}
        self.generation = None
        self.seen = set()

    def read(self, now=None, backend_start=None):
        now = time.time() if now is None else now
        if backend_start is None:
            backend_start = desktop_backend_start(now)
        if not backend_start:
            self.files.clear()
            return {"t": "activity", "n": 0, "at": int(now)}, []
        # ps elapsed is rounded to seconds, so tolerate one second of jitter.
        if self.generation is None or abs(backend_start - self.generation) > 2:
            self.files.clear()
            self.generation = backend_start
        db = max(self.root.glob("state_*.sqlite"), key=lambda p: int(p.stem.split("_")[-1]))
        con = sqlite3.connect("file:%s?mode=ro" % db, uri=True, timeout=0.3)
        try:
            rows = con.execute("SELECT id,rollout_path,title FROM threads WHERE archived=0 "
                               "AND source='vscode' AND originator='Codex Desktop' AND updated_at>=? "
                               "AND NOT EXISTS (SELECT 1 FROM thread_spawn_edges e WHERE e.child_thread_id=threads.id)",
                               (int(backend_start) - 2,)).fetchall()
        finally:
            con.close()
        alive, done, known = set(), [], True
        for sid, filename, title in rows:
            path = Path(filename)
            alive.add(sid)
            st = path.stat()
            rec = self.files.get(sid)
            if rec is None or rec["size"] > st.st_size or rec["inode"] != st.st_ino:
                # Ignore the last partial line; the next poll reads it whole.
                with path.open("rb") as f:
                    f.seek(max(0, st.st_size - 131072))
                    tail = f.read()
                end = st.st_size - len(tail.rsplit(b"\n", 1)[-1])
                found = latest_event(path, end)
                rec = {"size": end, "inode": st.st_ino, "active": False, "turn": None}
                self.files[sid] = rec
                events = [found] if found else []
            else:
                events = []
                with path.open("rb") as f:
                    f.seek(rec["size"])
                    while True:
                        line = f.readline()
                        if not line or not line.endswith(b"\n"):
                            break
                        rec["size"] = f.tell()
                        found = event(line)
                        if found:
                            events.append(found)
            for stamp, payload in events:
                if stamp < backend_start - 2:
                    continue
                typ, turn = payload["type"], payload.get("turn_id")
                if typ == "task_started":
                    rec.update(active=True, turn=turn)
                elif not rec["turn"] or not turn or rec["turn"] == turn:
                    rec["active"] = False
                if typ == "task_complete" and stamp > self.started:
                    item = completion(sid, title, stamp, payload)
                    if item["id"] not in self.seen:
                        self.seen.add(item["id"])
                        done.append(item)
            if rec["active"] and now - st.st_mtime > 900:
                known = False  # stale or orphaned turn: never invent an idle verdict
        self.files = {sid: rec for sid, rec in self.files.items() if sid in alive}
        if len(self.seen) > 2048:
            self.seen = {item["id"] for item in done}
        return {"t": "activity", "n": sum(r["active"] for r in self.files.values()) if known else None,
                "at": int(now)}, done
