# Codex-only fork

This fork defaults to a single Codex seat, including while disconnected. It keeps
the upstream five wire/report indices so existing messages and saved skins remain
compatible. Other agents stay off and are omitted from the settings seat picker.
The host skips their process matching, log polling and Claude session tracking.

`host/setup.sh` installs only Codex hooks; it does not change Claude settings.
The default focus shortcut is empty: configure a shortcut only after assigning
and verifying it in your desktop app. The default Codex app remains ChatGPT;
change `focus_apps.codex` to Codex in the settings page if using Codex.app.

Build and flash with the upstream `amoled216` environment. Back up the entire
device flash with esptool before the first upload. A microSD card is optional for
boot, clock and usage pages; full fonts and the SD-backed OTA flow require FAT32.

Run the checks separately so upstream multi-agent fixtures do not replace the
fork's defaults:

```sh
/usr/bin/python3 host/test_host.py
/usr/bin/python3 host/test_codex_only.py
/usr/bin/python3 host/test_codex_usage.py
```

macOS Bluetooth and Accessibility permissions and Codex hook trust require the
user's review. The approve bubble still sends the configured key to the focused
desktop app; verify this on a harmless permission request before relying on it.

The clock and calendar show `YYYYMMDD`, with a 24-hour clock and weekday instead
of sexagenary dates / double-hour names. Almanac and podcast pages are removed
from navigation. Podcast HTTP operations, player startup/resume, automatic
podcast/almanac card pushes and fortune voice prefetch are disabled. The Mac
now-playing page and pet audio remain available.

From the clock, swipe right for **Codex usage**, left to return. Swipe left from
the clock for Mac now-playing. The speaker-side orientation now shows the normal
calendar. On the Mac, open `http://127.0.0.1:8788/settings#usage` for exact counts.
No card is needed for usage; account access requires internet on the Mac.

- Quota: read-only `account/rateLimits/read` via the installed Codex app-server,
  using the existing CLI login. Prefer the `codex` entry in `rateLimitsByLimitId`;
  display only actual primary/secondary windows and reset time, with the longest
  window as the hero. The large percentage and progress bar are **consumed**
  quota; the small label says **remaining**.
  Refresh every five minutes. No API key, login changes or reset-credit spending.
  Official protocol: https://developers.openai.com/codex/app-server#auth-endpoints
- Tokens: `account/usage/read` official account `dailyUsageBuckets`, including
  other devices covered by that account service. Match today's calendar-date
  label and sum Monday through today; bucket timezone/attribution belongs to
  the service, and is not reinterpreted as a Beijing-time log scan.
  The service can publish with a delay. Missing today/week buckets are **pending**,
  not zero. Only an explicit zero bucket produces zero.
- Both endpoints refresh every five minutes and fail independently; there is no
  local-log fallback or mixed-account aggregation. The board receives a heartbeat
  every 30 seconds containing only totals, dates and quota windows, not credentials
  or conversation text. Reset timestamps are displayed in UTC+8.
- Unknown quotas/counts show unavailable / `--`, not zero. Old account data is marked
  stale after ten minutes; host delivery after 90 seconds. The board
  abbreviates counts as K/M/B; the Mac page shows full integers.

After updating the host manually, copy **both** `agentpet_host.py` and
`codex_usage.py`, plus `firmware/fonts/IBMPlexSans-{Bold,SemiBold}.ttf` and
`firmware/fonts/LICENSE-OFL.txt` into `~/.agentpet/`, then restart
`com.agentpet.host`. `host/setup.sh` includes these files automatically.

Usage-page visual reference: [VibePulse](https://github.com/niclasvestlund-YT/vibepulse),
commit `21b905d206ce85ca18a95b17809ad67eb8ace96a`, especially the Codex weekly
screen and full-screen quota specification. This fork uses its black/white/purple
hierarchy and IBM Plex typography while keeping AgentTouch's graphics stack and
pet. No VibePulse application code is copied. IBM Plex comes from the IBM project
under the bundled SIL OFL license in `firmware/fonts/LICENSE-OFL.txt`.
