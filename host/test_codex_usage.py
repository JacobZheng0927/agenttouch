import datetime as dt
import subprocess
import sys
import unittest
from unittest import mock

from codex_usage import account_tokens, read_account_usage, quota_windows
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


if __name__ == '__main__':
    unittest.main()
