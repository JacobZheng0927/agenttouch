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

The clock and task page show `YYYYMMDD`, with a 24-hour clock and weekday instead
of sexagenary dates / double-hour names. Almanac and podcast pages are removed
from navigation. Podcast HTTP operations, player startup/resume, automatic
podcast/almanac card pushes and fortune voice prefetch are disabled. The Mac
now-playing page and pet audio remain available.

With the buttons on the left, the default page is **Codex usage**. Swipe left
for the clock, then left again for Mac now-playing; swipe right to go back. The orientation with the three buttons on the right now shows the
task list. On the Mac, open `http://127.0.0.1:8788/settings#usage` for exact counts.
No card is needed for usage; account access requires internet on the Mac.

- Quota: read-only `account/rateLimits/read` via the installed Codex app-server,
  using the existing CLI login. Prefer the `codex` entry in `rateLimitsByLimitId`;
  display only actual primary/secondary windows and reset time, with the longest
  window as the hero. The large percentage and progress bar are **consumed**
  quota; the small label says **remaining**.
  Refresh every five minutes. No API key, login changes or reset-credit spending.
  Official protocol: https://developers.openai.com/codex/app-server#auth-endpoints
- Today tokens: local Codex desktop `token_usage_record` response records in
  `~/.codex/sessions`, summed by their timestamps in UTC+8. Use each response's
  `usage.total_tokens` once by `response_id`, including cached input already
  present in that total; never sum cumulative thread/turn counters or the parallel
  legacy `token_count` stream. Refresh every 30 seconds with incremental reads.
  This is a local log count: other devices and calls without desktop response
  records are excluded. Empty/unsupported logs show unavailable. Replay copies
  share response IDs; the earliest observed timestamp wins, but replays whose
  original file is no longer available can still affect date attribution.
- Week tokens: `account/usage/read` official account `dailyUsageBuckets`, including
  other devices covered by that account service. Match today's calendar-date
  label and sum Monday through today; bucket timezone/attribution belongs to
  the service, and is not reinterpreted as a Beijing-time log scan.
  The service can publish with a delay. Missing week buckets are **pending**,
  not zero. Only an explicit zero bucket produces zero.
- Account endpoints refresh every five minutes and fail independently. Today's
  local and this week's account counts intentionally have different scopes and
  are labeled separately; do not add them together. The board receives a heartbeat
  every 30 seconds containing only totals, dates and quota windows, not credentials
  or conversation text. Reset timestamps are displayed in UTC+8.
- Unknown quotas/counts show unavailable / `--`, not zero. Old account data is marked
  stale after ten minutes; host delivery after 90 seconds. The board
  abbreviates counts as K/M/B; the Mac page shows full integers.

The working pet remains on the face page: stand the board with its three keys
at the top. Side placement shows clock/usage/tasks. Working and done do not
override a deliberate page; needs_you can. To keep the pet while turning the
board, hold the left key for one second on the face page to pin it (hold again
to unpin).

After updating the host manually, copy `agentpet_host.py`, `codex_usage.py` and
`todo_store.py` and `codex_activity.py`, plus `firmware/fonts/IBMPlexSans-{Bold,SemiBold}.ttf` and
`firmware/fonts/LICENSE-OFL.txt` into `~/.agentpet/`, then restart
`com.agentpet.host`. `host/setup.sh` includes these files automatically.

Usage-page visual reference: [VibePulse](https://github.com/niclasvestlund-YT/vibepulse),
commit `21b905d206ce85ca18a95b17809ad67eb8ace96a`, especially the Codex weekly
screen and full-screen quota specification. This fork uses its black/white/purple
hierarchy and IBM Plex typography while keeping AgentTouch's graphics stack and
pet. No VibePulse application code is copied. IBM Plex comes from the IBM project
under the bundled SIL OFL license in `firmware/fonts/LICENSE-OFL.txt`.


## Local tasks

Place the three buttons on the **right** to open the task list, replacing the
calendar in that orientation. Open `http://127.0.0.1:8788/settings#todo` on the Mac
to add, edit, pin and delete tasks. Changes save immediately to
`~/.agentpet/todos.json`; no cloud account or SD card is needed. Maximum 16 tasks,
48 characters and 144 UTF-8 bytes per title. Common simplified Chinese and ASCII
are included in flash; unsupported glyphs and emoji appear as `?`.

The board shows three tasks per page. Swipe vertically to browse; tap the circle
to complete or restore, or tap the title for details. Pinned tasks come first
within unfinished/completed groups. The last change can be undone for ten seconds,
including changes made on the Mac. Concurrent edits are rejected and refreshed
using a revision number, so a stale screen cannot overwrite newer tasks.

Details offer pin/unpin and a 25-minute focus timer. The timer is local to the
board and resets when restarted; it does not control Codex or the Mac. After
90 seconds without a host update, the list becomes read-only until reconnected.
Task changes are acknowledged only after the Mac has saved them; requests time
out after five seconds. Invalid persisted data is preserved and reported rather
than replaced with an empty list.

Completion shows an immediate check mark and a saving indicator while waiting
for the Mac. Repeated taps during that request are ignored, and a failed or timed
out save returns to the confirmed state. BLE writes are serialized across whole
JSON lines so a task acknowledgement cannot be interleaved with usage/status
updates. `host/test_todo.py` covers concurrent fragmented BLE writes;
`host/test_todo_touch.py` exercises the actual task hit handler against the
Arduino_Canvas pixel rotation in all four orientations (requires clang++ and
PlatformIO's downloaded dependencies).

All common pages show a battery badge at the top right: percentage when a
battery is detected, a lightning mark while charging, USB when externally
powered without a detected battery, and -- when the level is unknown. The PMU
reading is cached from the existing two-second power poll. Dates move left to
leave the status badge clear.

The face page keeps a small dot and the number of running Codex Desktop main
threads at the top left. The host reads the local state index read-only and
incrementally follows task_started/task_complete/turn_aborted events; archived
threads and spawned agents are excluded. The desktop backend must be running.
Counts refresh every two seconds and heartbeat every 15 seconds. An active
rollout quiet for 15 minutes makes the count unknown; unreadable data does too.
The board shows -- after 45 seconds without an activity update or when offline.
This is this Mac's active-turn count, not all saved chats or an account total.
An open turn waiting on a tool/approval is still an ongoing turn.

Each completed turn produces one transient notice, deduplicated by thread and
turn ID on the host and across TCP/BLE on the board. Old completions are not
replayed when the host starts or a board reconnects. Notices on the face page
merge within a fixed eight-second window; a new completion does not extend it.
They never switch orientation pages or displace a permission prompt. Tap the
notice to open the title, completion time/duration and final-reply preview.
Swipe vertically for pages, horizontally for replies; tap < to return. An open
preview stays until returned and receives no text replacements. The transient
group retains the latest four previews, with no unread counter or persistent
queue. Previews are plain text capped at 600 UTF-8 bytes (ellipsis when trimmed),
and titles at 144 bytes; no conversation history or credentials are sent.
Run host/test_codex_activity.py and host/test_activity_touch.py for lifecycle,
deduplication, expiry, merging and rotated detail-hit checks.
For USB layout captures, the existing debug view message accepts
`{"t":"view","p":"face","s":20,"reply":true}` to tap an active completion
notice through its real hit handler; reply:false taps Back. It does not create
or replay a completion.
