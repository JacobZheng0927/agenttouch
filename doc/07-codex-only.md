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
boot and the pet; full fonts, podcasts and the SD-backed OTA flow require FAT32.

Run the checks separately so upstream multi-agent fixtures do not replace the
fork's defaults:

```sh
/usr/bin/python3 host/test_host.py
/usr/bin/python3 host/test_codex_only.py
```

macOS Bluetooth and Accessibility permissions and Codex hook trust require the
user's review. The approve bubble still sends the configured key to the focused
desktop app; verify this on a harmless permission request before relying on it.

Codex account quotas are not added by this fork. It shows agent activity and keeps
the upstream pet, clock, calendar, audio and power features.
