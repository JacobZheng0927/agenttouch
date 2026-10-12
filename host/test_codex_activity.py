import datetime
import json
import os
from pathlib import Path
import sqlite3
import tempfile
import unittest
from unittest.mock import patch
from codex_activity import CodexActivity, desktop_backend_start, clipped

class ActivityTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(); self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.con = sqlite3.connect(self.root / "state_5.sqlite")
        self.addCleanup(self.con.close)
        self.con.execute("CREATE TABLE threads(id,rollout_path,title,archived,source,originator,updated_at)")
        self.con.execute("CREATE TABLE thread_spawn_edges(child_thread_id)")
        self.reader = CodexActivity(self.root); self.reader.started = 100
    def add(self, sid, typ="task_started", stamp=90, source="vscode"):
        p = self.root / (sid + ".jsonl")
        self.con.execute("INSERT INTO threads VALUES (?,?,?,0,?,'Codex Desktop',150)", (sid,str(p),"标题",source)); self.con.commit()
        self.append(sid,typ,stamp)
        return p
    def append(self,sid,typ,stamp,turn="turn",**extra):
        d={"type":"event_msg","timestamp":datetime.datetime.fromtimestamp(stamp,datetime.timezone.utc).isoformat(),"payload":dict(type=typ,turn_id=turn,**extra)}
        p=self.root/(sid+".jsonl")
        with p.open("ab") as f:f.write((json.dumps(d,ensure_ascii=False)+"\n").encode())
        os.utime(p,(stamp,stamp))
    def read(self, now=150, generation=50):return self.reader.read(now, generation)
    def test_two_active_then_completion_and_no_replay(self):
        self.add("a");self.add("b");self.add("internal",source="exec")
        self.assertEqual(self.read()[0]["n"],2)
        self.append("a","task_complete",120,last_agent_message="最终回复",duration_ms=3500)
        snap,done=self.read();self.assertEqual(snap["n"],1);self.assertEqual(done[0]["body"],"最终回复")
        self.assertIn("0m03s",done[0]["meta"]);self.assertEqual(self.read()[1],[])
        self.append("a","task_complete",121,last_agent_message="重复")
        self.assertEqual(self.read()[1],[])
    def test_startup_never_replays_old_completion(self):
        self.add("a","task_complete",90)
        self.assertEqual(self.read(),({"t":"activity","n":0,"at":150},[]))
    def test_abort_and_old_turn_completion(self):
        self.add("a");self.read()
        self.append("a","task_started",110,turn="new")
        self.append("a","turn_aborted",111,turn="turn")
        self.assertEqual(self.read()[0]["n"],1)
        self.append("a","turn_aborted",112,turn="new")
        self.assertEqual(self.read()[0]["n"],0)
    def test_partial_line_and_archive(self):
        p=self.add("a");self.read()
        raw=json.dumps({"type":"event_msg","timestamp":"1970-01-01T00:02:00Z","payload":{"type":"task_complete","turn_id":"turn"}}).encode()
        with p.open("ab") as f:f.write(raw[:30])
        self.assertEqual(self.read()[0]["n"],1)
        with p.open("ab") as f:f.write(raw[30:]+b"\n")
        self.assertEqual(self.read()[0]["n"],0)
        self.con.execute("UPDATE threads SET archived=1");self.con.commit();self.read();self.assertEqual(self.reader.files,{})
    def test_stale_is_unknown_and_backend_restart_ignores_old_turn(self):
        self.add("a")
        self.assertIsNone(self.read(now=1200)[0]["n"])
        self.assertEqual(self.read(generation=100)[0]["n"],0)
    def test_subagent_is_excluded(self):
        self.add("child");self.con.execute("INSERT INTO thread_spawn_edges VALUES ('child')");self.con.commit()
        self.assertEqual(self.read()[0]["n"],0)
    def test_bootstrap_reverse_blocks_and_utf8_preview_bound(self):
        p=self.add("a")
        with p.open("ab") as f:f.write((b'{"type":"token_usage_record"}\n')*15000)
        self.assertEqual(self.read()[0]["n"],1)
        self.append("a","task_complete",120,last_agent_message="字"*10000+"<oai-mem-citation>private</oai-mem-citation>")
        done=self.read()[1][0]
        self.assertLessEqual(len(done["body"].encode()),600)
        self.assertLess(len(json.dumps({"t":"completed","item":done},ensure_ascii=False).encode()),4096)
        self.assertNotIn("private",done["body"])
    def test_backend_process_detection(self):
        with patch("codex_activity.subprocess.check_output",return_value="01:02 /Applications/ChatGPT.app/Contents/Resources/codex app-server --listen stdio://\n"):
            self.assertEqual(desktop_backend_start(100),38)
        with patch("codex_activity.subprocess.check_output",return_value="01:02 /tmp/codex app-server\n"):
            self.assertIsNone(desktop_backend_start(100))
if __name__ == '__main__':unittest.main()
