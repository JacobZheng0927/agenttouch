import http.client
import json
from pathlib import Path
import tempfile
import threading
import unittest
from unittest import mock
from http.server import ThreadingHTTPServer

import agentpet_host as H
from todo_store import TodoStore, todo_line


class TodoTests(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.path = Path(tmp.name) / 'todos.json'
        self.store = TodoStore(self.path)

    def act(self, **command):
        return self.store.apply(dict(rev=self.store.rev, **command))

    def test_save_edit_complete_restart_and_undo(self):
        row = self.act(op='add', title='  写自己的固件  ')
        tid = row['items'][0]['id']
        self.assertEqual(row['items'][0]['title'], '写自己的固件')
        self.act(op='set', id=tid, title='验证中文显示', important=True)
        self.act(op='set', id=tid, done=True)
        self.assertTrue(TodoStore(self.path).snapshot()['items'][0]['done'])
        self.assertTrue(self.act(op='undo')['ok'])
        self.assertFalse(self.store.snapshot()['items'][0]['done'])
        self.assertTrue(self.store.snapshot()['items'][0]['important'])

    def test_remove_and_undo_without_losing_order(self):
        self.act(op='add', title='A')
        self.act(op='add', title='B')
        before = self.store.snapshot()['items']
        self.act(op='remove', id=before[0]['id'])
        self.assertEqual(len(self.store.snapshot()['items']), 1)
        self.assertEqual(self.act(op='undo')['items'], before)
        self.assertFalse(self.act(op='undo')['ok'])

    def test_invalid_input_and_capacity_do_not_write(self):
        for value in (None, '', ' ', '\n', 'a\nb', 'x'*49, '😀'*48):
            self.assertFalse(self.act(op='add', title=value)['ok'])
        self.assertFalse(self.path.exists())
        for _ in range(16):
            self.assertTrue(self.act(op='add', title='任务')['ok'])
        self.assertFalse(self.act(op='add', title='溢出')['ok'])
        tid = self.store.items[0]['id']
        self.assertFalse(self.act(op='set', id=tid, done='false')['ok'])

    def test_stale_revision_and_duplicate_retry_do_not_overwrite(self):
        original = {'rev': 0, 'op': 'add', 'title': 'one'}
        self.assertTrue(self.store.apply(original)['ok'])
        self.assertFalse(self.store.apply(original)['ok'])
        self.assertEqual(len(self.store.items), 1)
        self.assertFalse(self.act(op='set', id='missing', done=True)['ok'])

    def test_expired_undo_and_save_failure_preserve_data(self):
        self.act(op='add', title='original')
        original = self.path.read_bytes()
        with mock.patch('todo_store.time.monotonic', return_value=self.store.undo[0] + 1):
            self.assertFalse(self.store.snapshot()['undo'])
            self.assertFalse(self.act(op='undo')['ok'])
        with mock.patch.object(Path, 'replace', side_effect=OSError('disk failure')):
            self.assertFalse(self.act(op='add', title='lost')['ok'])
        self.assertEqual(self.path.read_bytes(), original)
        self.assertEqual(len(self.store.items), 1)

    def test_corrupt_file_is_not_overwritten(self):
        self.path.write_text('bad json')
        store = TodoStore(self.path)
        self.assertTrue(store.snapshot()['error'])
        self.assertFalse(store.apply({'rev': 0, 'op': 'add', 'title': 'new'})['ok'])
        self.assertEqual(self.path.read_text(), 'bad json')

    def test_maximum_wire_snapshot_fits_firmware_line(self):
        for _ in range(16):
            result = self.act(op='add', title='任'*48)
        self.assertLess(len(todo_line(result)), 4096)
        self.assertEqual(json.loads(todo_line(result))['items'][0]['title'], '任'*48)

    def test_board_mutation_uses_same_store_and_broadcasts_ack(self):
        self.act(op='add', title='from Mac')
        tid = self.store.items[0]['id']
        with mock.patch.object(H, 'todos', self.store), mock.patch.object(H, 'push_raw') as push:
            H.handle_board_msg(json.dumps({'t': 'todo_action', 'op': 'set', 'rev': 1,
                                          'id': tid, 'done': True}).encode(), lambda _: None)
        self.assertTrue(json.loads(push.call_args[0][0])['items'][0]['done'])
        self.assertTrue(TodoStore(self.path).items[0]['done'])

    def test_http_add_and_cross_origin_rejection(self):
        server = ThreadingHTTPServer(('127.0.0.1', 0), H.HookHandler)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            with mock.patch.object(H, 'todos', self.store), mock.patch.object(H, 'push_raw'):
                conn = http.client.HTTPConnection('127.0.0.1', server.server_port)
                headers = {'Host': '127.0.0.1:8788', 'Content-Type': 'application/json'}
                conn.request('POST', '/todo', json.dumps({'op': 'add', 'rev': 0, 'title': '<script>测试</script>'}), headers)
                response = conn.getresponse()
                self.assertEqual(response.status, 200)
                self.assertTrue(json.loads(response.read())['ok'])
                headers['Origin'] = 'https://evil.example'
                conn.request('POST', '/todo', '{}', headers)
                response = conn.getresponse()
                self.assertEqual(response.status, 403)
                response.read()
                self.assertEqual(len(self.store.items), 1)
                conn.close()
        finally:
            server.shutdown()
            server.server_close()


if __name__ == '__main__':
    unittest.main()
