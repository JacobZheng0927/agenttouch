"""Read-only account quota and daily tokens through the official Codex protocol."""
import datetime as dt
import json
import os
from pathlib import Path
import selectors
import shutil
import subprocess
import time

TZ = dt.timezone(dt.timedelta(hours=8))


class LocalTodayTokens:
    """Desktop response records only; response_id also deduplicates fork replays."""
    def __init__(self, root):
        self.root = Path(root)
        self.files = {}
        self.day = None

    def read(self, today):
        if not self.root.is_dir():
            return None
        if today != self.day:
            self.files.clear()
            self.day = today
        start = dt.datetime.combine(today, dt.time(), TZ).timestamp()
        live = set()
        for path in self.root.rglob('*.jsonl'):
            stat = path.stat()
            if stat.st_mtime < start:
                continue
            live.add(path)
            identity = (stat.st_dev, stat.st_ino)
            entry = self.files.get(path)
            if not entry or entry['identity'] != identity or stat.st_size < entry['offset']:
                entry = self.files[path] = {'identity': identity, 'offset': 0, 'records': {}}
            with path.open('rb') as handle:
                handle.seek(entry['offset'])
                while True:
                    line = handle.readline()
                    if not line.endswith(b'\n'):
                        break  # Retry an incomplete append next time.
                    entry['offset'] = handle.tell()
                    try:
                        row = json.loads(line)
                        if row.get('type') != 'token_usage_record':
                            continue
                        payload = row['payload']
                        rid = payload['response_id']
                        tokens = payload['usage']['total_tokens']
                        stamp = dt.datetime.fromisoformat(row['timestamp'].replace('Z', '+00:00'))
                        if (not isinstance(rid, str) or not rid or stamp.tzinfo is None or
                                not isinstance(tokens, int) or isinstance(tokens, bool) or tokens < 0):
                            continue
                        # Keep original dates too, so a retimestamped replay cannot move a
                        # known older response into today. Cached/reasoning subsets aren't added.
                        prev = entry['records'].get(rid)
                        if prev is None or stamp < prev[0]:
                            entry['records'][rid] = (stamp, tokens)
                    except (ValueError, TypeError, KeyError, AttributeError):
                        continue
        self.files = {p: v for p, v in self.files.items() if p in live}
        records = {}
        for entry in self.files.values():
            for rid, record in entry['records'].items():
                if rid not in records or record[0] < records[rid][0]:
                    records[rid] = record
        # An existing but unsupported/empty log is unknown, rather than a false zero.
        return (sum(n for stamp, n in records.values() if stamp.astimezone(TZ).date() == today)
                if records else None)


def read_account_usage():
    # Prefer the desktop's CLI: Homebrew may lag its account API/schema.
    candidates = [Path('/Applications') / app / 'Contents/Resources/codex-cli/CodexCLI.app/Contents/MacOS/codex'
                  for app in ('ChatGPT.app', 'Codex.app')]
    binary = next((str(p) for p in candidates if p.is_file()), shutil.which('codex'))
    if not binary:
        raise RuntimeError('Codex CLI unavailable')
    p = subprocess.Popen([binary, 'app-server'], stdin=subprocess.PIPE,
                         stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, bufsize=0)
    def send(r):
        p.stdin.write((json.dumps(r) + '\n').encode())
    try:
        send({'id': 1, 'method': 'initialize', 'params': {
            'clientInfo': {'name': 'agenttouch_usage', 'version': '1.0'},
            'capabilities': {'experimentalApi': True}}})
        replies, pending = {}, b''
        with selectors.DefaultSelector() as sel:
            sel.register(p.stdout, selectors.EVENT_READ)
            deadline = time.monotonic() + 25
            while time.monotonic() < deadline:
                if not sel.select(1):
                    continue
                chunk = os.read(p.stdout.fileno(), 65536)
                if not chunk:
                    break
                pending += chunk
                while b'\n' in pending:
                    line, pending = pending.split(b'\n', 1)
                    r = json.loads(line)
                    if r.get('id') == 1:
                        if 'error' in r:
                            raise RuntimeError('Codex initialization failed')
                        send({'method': 'initialized', 'params': {}})
                        send({'id': 2, 'method': 'account/rateLimits/read'})
                        send({'id': 3, 'method': 'account/usage/read'})
                    elif r.get('id') in (2, 3):
                        replies[r['id']] = r.get('result')
                if len(replies) == 2:
                    break
        # Preserve either independently successful endpoint if the other fails.
        return replies.get(2), replies.get(3)
    finally:
        p.terminate()
        try:
            p.communicate(timeout=3)
        except subprocess.TimeoutExpired:
            p.kill()
            p.communicate()


def quota_windows(result):
    if not isinstance(result, dict):
        return []
    buckets = result.get('rateLimitsByLimitId')
    bucket = buckets.get('codex') if isinstance(buckets, dict) else result.get('rateLimits')
    windows = []
    for key in ('primary', 'secondary'):
        w = (bucket or {}).get(key)
        if not w or w.get('usedPercent') is None:
            continue
        mins = w.get('windowDurationMins')
        label = ('%gd' % (mins / 1440) if mins and mins % 1440 == 0 else
                 '%gh' % (mins / 60) if mins and mins % 60 == 0 else
                 '%gm' % mins if mins else key)
        reset = w.get('resetsAt')
        windows.append((mins or 0, {'label': label, 'left': max(0, min(100, 100 - w['usedPercent'])),
                        'reset': dt.datetime.fromtimestamp(reset, TZ).strftime('%Y%m%d %H:%M') if reset else '--'}))
    return [w for _, w in sorted(windows, key=lambda pair: pair[0], reverse=True)]


def account_tokens(result, today):
    """Sum service date buckets without claiming a timezone or filling gaps with 0."""
    buckets = result.get('dailyUsageBuckets') if isinstance(result, dict) else None
    if not isinstance(buckets, list):
        return None, None
    days = {}
    for row in buckets:
        if not isinstance(row, dict):
            continue
        try:
            day = dt.date.fromisoformat(row['startDate'])
            tokens = row['tokens']
        except (ValueError, TypeError, KeyError):
            continue
        if isinstance(tokens, int) and not isinstance(tokens, bool) and tokens >= 0:
            days[day] = tokens
    week = today - dt.timedelta(days=today.weekday())
    values = [n for day, n in days.items() if week <= day <= today]
    return days.get(today), sum(values) if values else None
