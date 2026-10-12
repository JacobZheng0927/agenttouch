"""Small local task list. Save atomically before acknowledging either UI."""
import copy
import datetime
import json
from pathlib import Path
import threading
import time
import uuid


class TodoStore:
    LIMIT = 16

    def __init__(self, path):
        self.path = Path(path)
        self.lock = threading.Lock()
        self.items, self.rev, self.undo, self.error = [], 0, None, ''
        try:
            data = json.loads(self.path.read_text())
            if type(data['rev']) is not int or not 0 <= data['rev'] < 2**32:
                raise ValueError()
            if not isinstance(data['items'], list) or len(data['items']) > self.LIMIT:
                raise ValueError()
            seen = set()
            for item in data['items']:
                if (not isinstance(item['id'], str) or len(item['id']) != 12 or item['id'] in seen or
                        any(c not in '0123456789abcdef' for c in item['id'])):
                    raise ValueError()
                self.title(item['title'])
                if type(item['done']) is not bool or type(item['important']) is not bool:
                    raise ValueError()
                seen.add(item['id'])
            self.items, self.rev = data['items'], data['rev']
        except FileNotFoundError:
            pass
        except (OSError, ValueError, KeyError, TypeError):
            self.error = '待办文件无法读取，已保留原文件，请检查 todos.json。'

    @staticmethod
    def title(value):
        if not isinstance(value, str):
            raise ValueError('请输入任务名称。')
        value = value.strip()
        if not value or len(value) > 48 or len(value.encode('utf-8')) > 144 or any(ord(c) < 32 for c in value):
            raise ValueError('任务名称需为 1–48 字，不能包含换行。')
        return value

    def snapshot(self):
        with self.lock:
            return self._snapshot()

    def _snapshot(self):
        undo_ms = max(0, int((self.undo[0] - time.monotonic()) * 1000)) if self.undo else 0
        return {'t': 'todo', 'rev': self.rev, 'items': copy.deepcopy(self.items),
                'date': datetime.datetime.now(datetime.timezone(datetime.timedelta(hours=8))).strftime('%Y%m%d'),
                'undo': undo_ms > 0, 'undo_ms': undo_ms, 'error': self.error}

    def apply(self, command):
        with self.lock:
            try:
                if self.error:
                    raise ValueError(self.error)
                if not isinstance(command, dict) or type(command.get('rev')) is not int or command['rev'] != self.rev:
                    raise ValueError('列表已更新，请重试。')
                items = copy.deepcopy(self.items)
                op = command.get('op')
                if op == 'add':
                    if len(items) >= self.LIMIT:
                        raise ValueError('最多保留 16 条任务，请先移除已完成任务。')
                    items.append({'id': uuid.uuid4().hex[:12], 'title': self.title(command.get('title')),
                                  'done': False, 'important': False})
                elif op == 'undo':
                    if not self.undo or time.monotonic() >= self.undo[0]:
                        raise ValueError('撤销时间已过。')
                    items = copy.deepcopy(self.undo[1])
                elif op in ('set', 'remove'):
                    item = next((i for i in items if i['id'] == command.get('id')), None)
                    if item is None:
                        raise ValueError('任务已移除。')
                    if op == 'remove':
                        items.remove(item)
                    else:
                        for key in ('done', 'important', 'title'):
                            if key in command:
                                if key == 'title':
                                    item[key] = self.title(command[key])
                                elif type(command[key]) is bool:
                                    item[key] = command[key]
                                else:
                                    raise ValueError('任务状态无效。')
                else:
                    raise ValueError('未知操作。')
                rev = (self.rev + 1) % 2**32
                self.path.parent.mkdir(parents=True, exist_ok=True)
                tmp = self.path.with_suffix('.tmp')
                tmp.write_text(json.dumps({'rev': rev, 'items': items}, ensure_ascii=False), encoding='utf-8')
                tmp.replace(self.path)
                self.undo = None if op == 'undo' else (time.monotonic() + 10, self.items)
                self.items, self.rev = items, rev
                return dict(self._snapshot(), ok=True)
            except (ValueError, OSError) as exc:
                return dict(self._snapshot(), ok=False, error=str(exc))


def todo_line(snapshot):
    # Compact UTF-8 keeps even 16 maximal titles below the board's 4096-byte line limit.
    return (json.dumps(snapshot, ensure_ascii=False, separators=(',', ':')) + '\n').encode()
