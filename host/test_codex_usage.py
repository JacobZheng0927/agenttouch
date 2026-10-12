import datetime as dt
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

from codex_usage import account_tokens, read_account_usage, quota_windows, LocalTodayTokens
import agentpet_host as H


class TokensTests(unittest.TestCase):
    def setUp(self):
        self.today = dt.date(2026, 10, 14)

    def test_account_dates_and_monday_start(self):
        result = {'dailyUsageBuckets': [
            {'startDate': '2026-10-11', 'tokens': 1000},
            {'startDate': '2026-10-12', 'tokens': 100},
            {'startDate': '2026-10-13', 'tokens': 200},
            {'startDate': '2026-10-14', 'tokens': 300},
            {'startDate': '2026-10-15', 'tokens': 9000}]}
        self.assertEqual(account_tokens(result, self.today), (300, 600))

    def test_absent_buckets_and_unpublished_today_are_not_zero(self):
        for result in (None, {}, {'dailyUsageBuckets': None}, {'dailyUsageBuckets': []}):
            self.assertEqual(account_tokens(result, self.today), (None, None))
        result = {'dailyUsageBuckets': [{'startDate': '2026-10-13', 'tokens': 200}]}
        self.assertEqual(account_tokens(result, self.today), (None, 200))

    def test_explicit_zero_and_malformed_rows(self):
        result = {'dailyUsageBuckets': [None, {'startDate': 'bad', 'tokens': 99},
            {'startDate': '2026-10-12', 'tokens': -5},
            {'startDate': '2026-10-13', 'tokens': True},
            {'startDate': '2026-10-14', 'tokens': 0}]}
        self.assertEqual(account_tokens(result, self.today), (0, 0))

    def test_new_monday_does_not_include_last_week(self):
        result = {'dailyUsageBuckets': [{'startDate': '2026-10-11', 'tokens': 99}]}
        self.assertEqual(account_tokens(result, dt.date(2026, 10, 12)), (None, None))


class QuotaTests(unittest.TestCase):
    def test_codex_bucket_preferred_and_missing_windows_not_zero(self):
        self.assertEqual(quota_windows({'rateLimitsByLimitId': {}, 'rateLimits': {
            'primary': {'usedPercent': 99}}}), [])
        result = {'rateLimitsByLimitId': {'codex': {'primary': {
            'usedPercent': 6, 'windowDurationMins': 10080, 'resetsAt': 1792318929},
            'secondary': None}}}
        self.assertEqual(quota_windows(result), [{'label': '7d', 'left': 94, 'reset': '20261018 18:22'}])

    def test_longest_window_is_hero_and_percent_clamped(self):
        result = {'rateLimits': {'primary': {'usedPercent': -4, 'windowDurationMins': 300},
                                'secondary': {'usedPercent': 101, 'windowDurationMins': 10080}}}
        self.assertEqual(quota_windows(result), [{'label': '7d', 'left': 0, 'reset': '--'},
                                                {'label': '5h', 'left': 100, 'reset': '--'}])
        self.assertEqual(quota_windows(None), [])

    def test_stale_or_unavailable_is_explicit(self):
        with mock.patch.object(H, 'usage', dict(H.usage, quota_at=0)):
            self.assertEqual(H.usage_snapshot()['status'], 'UNAVAILABLE')
        with mock.patch.object(H, 'usage', dict(H.usage, quota_at=100, tokens_at=100)), mock.patch.object(H.time, 'time', return_value=701):
            self.assertEqual(H.usage_snapshot()['status'], 'STALE')
            self.assertTrue(H.usage_snapshot()['tokens_stale'])

    def test_settings_has_usage_and_no_podcast_entry(self):
        html = H.settings_html()
        self.assertIn('data-page="usage"', html)
        self.assertIn('class="usage-hero"', html)
        self.assertNotIn('data-page="audio"', html)
        self.assertNotIn('local_stale', html)

    def test_account_rpc_handshake_partial_frames_and_independent_errors(self):
        # Exercise real pipes with a tiny JSONL server, without a network or login.
        fake = '''import json,sys
initialized=False
for line in sys.stdin:
 r=json.loads(line); method=r['method']
 if method=='initialize': out={'id':1,'result':{}}
 elif method=='initialized': initialized=True; continue
 else:
  assert initialized
  out=({'id':2,'error':{'message':'quota unavailable'}} if r['id']==2 else
       {'id':3,'result':{'dailyUsageBuckets':[{'startDate':'2026-10-14','tokens':123}]}})
 data=json.dumps(out)+'\\n'
 sys.stdout.write(data[:8]);sys.stdout.flush()
 sys.stdout.write(data[8:]);sys.stdout.flush()
'''
        real_popen = subprocess.Popen
        def spawn(_args, **kwargs):
            return real_popen([sys.executable, '-u', '-c', fake], **kwargs)
        with mock.patch('codex_usage.subprocess.Popen', side_effect=spawn):
            limits, tokens = read_account_usage()
        self.assertIsNone(limits)
        self.assertEqual(account_tokens(tokens, dt.date(2026, 10, 14)), (123, 123))


class LocalTokensTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.reader = LocalTodayTokens(self.root)
        self.today = dt.date(2026, 10, 12)

    def row(self, rid, tokens=100, stamp='2026-10-12T00:00:00Z'):
        return json.dumps({'type': 'token_usage_record', 'timestamp': stamp, 'payload': {
            'response_id': rid, 'usage': {'total_tokens': tokens, 'cached_input_tokens': 90},
            'thread_token_usage': {'total_tokens': 999999}}}) + '\n'

    def test_responses_not_cumulative_and_replays_count_once(self):
        (self.root / 'a.jsonl').write_text(self.row('a') + self.row('b', 200))
        (self.root / 'fork.jsonl').write_text(self.row('a') + self.row('c', 300))
        self.assertEqual(self.reader.read(self.today), 600)
        self.assertEqual(self.reader.read(self.today), 600)

    def test_beijing_midnight_and_old_response_retimestamp(self):
        old = self.row('old', 1000, '2026-10-11T15:59:59Z')
        (self.root / 'a.jsonl').write_text(old + self.row('new', 25, '2026-10-11T16:00:00Z'))
        (self.root / 'b.jsonl').write_text(self.row('old', 1000))
        self.assertEqual(self.reader.read(self.today), 25)

    def test_partial_append_and_invalid_rows(self):
        path = self.root / 'a.jsonl'
        record = self.row('b', 200)
        path.write_text(self.row('a') + 'bad json\n' + self.row('bad', True) + record[:20])
        self.assertEqual(self.reader.read(self.today), 100)
        with path.open('a') as handle:
            handle.write(record[20:])
        self.assertEqual(self.reader.read(self.today), 300)

    def test_truncated_or_replaced_file_is_reparsed(self):
        path = self.root / 'a.jsonl'
        path.write_text(self.row('a') + self.row('b'))
        self.assertEqual(self.reader.read(self.today), 200)
        path.write_text(self.row('a', 2))
        self.assertEqual(self.reader.read(self.today), 2)
        path.unlink()
        path.write_text(self.row('c', 7))
        self.assertEqual(self.reader.read(self.today), 7)

    def test_day_rollover_and_missing_unsupported_logs(self):
        self.assertIsNone(self.reader.read(self.today))
        (self.root / 'a.jsonl').write_text(self.row('a'))
        self.assertEqual(self.reader.read(self.today), 100)
        tomorrow = self.today + dt.timedelta(days=1)
        stamp = dt.datetime.combine(tomorrow, dt.time(), H.TZ).timestamp()
        os.utime(self.root / 'a.jsonl', (stamp, stamp))
        self.assertEqual(self.reader.read(tomorrow), 0)
        self.assertIsNone(LocalTodayTokens(self.root / 'missing').read(self.today))


if __name__ == '__main__':
    unittest.main()
