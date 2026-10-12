#!/usr/bin/python3
"""Codex-only defaults and isolation; no board or Mac UI required."""
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import agentpet_host as H


class CodexOnlyTests(unittest.TestCase):
    def test_defaults_and_wire_indices(self):
        self.assertEqual(H.ENABLED_AGENTS, {"codex"})
        self.assertEqual(H.active_agent, "codex")
        self.assertEqual(H.AGENTS.index("codex"), 1)
        self.assertEqual(len(H.report_msg()["w"]), len(H.AGENTS))
        self.assertEqual(H.FOCUS_KEYS, {})

    def test_front_app_ignores_other_seats(self):
        self.assertEqual(H.front_agent("ChatGPT|ChatGPT"), "codex")
        self.assertIsNone(H.front_agent("Warp|Warp"))
        self.assertIsNone(H.front_agent("Qoder CN IDE|Qoder"))

    def test_only_codex_process_is_monitored(self):
        ps = subprocess.CompletedProcess([], 0, stdout=(
            " 0.0 /Applications/ChatGPT.app/Contents/Resources/codex app-server\n"
            " 0.0 /usr/local/bin/claude\n"
            " 0.0 /Applications/Qoder CN IDE.app/Contents/MacOS/Qoder\n"))
        with mock.patch.object(H.subprocess, "run", return_value=ps):
            H.scan_processes()
        self.assertTrue(H.proc_seen["codex"])
        self.assertFalse(any(H.proc_seen[a] for a in H.AGENTS if a != "codex"))

    def test_other_agent_events_cannot_enable_a_seat(self):
        with tempfile.TemporaryDirectory() as td, \
                mock.patch.object(H, "CODEX_SESSIONS", Path(td)), \
                mock.patch.dict(H.proc_seen, {a: False for a in H.AGENTS}), \
                mock.patch.dict(H.claude_sessions, {
                    "other": {"state": "needs_you", "ts": H.time.time()}}):
            H.aggregate()
            self.assertTrue(all(H.state[a] == "off" for a in H.AGENTS if a != "codex"))

    def test_scan_lists_only_codex(self):
        self.assertEqual({row.get("seat") for row in H.scan_agents()}, {"codex"})


if __name__ == "__main__":
    unittest.main()
