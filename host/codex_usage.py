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
