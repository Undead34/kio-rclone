# Logs and safe diagnostics

KIO Rclone should be debuggable without collecting private cloud data. It has
no telemetry service and does not create a second OAuth-token store: rclone
keeps credentials in its own configuration.

## What the user sees today

- Dolphin receives readable KIO errors when rclone cannot list, read, write, or authenticate.
- Upload notifications receive rclone's live percentage, rate, ETA, and finalization state.
- Package builds keep the full CTest result in the terminal or CI log.

No persistent KIO Rclone debug log is created by default. This is intentional:
an always-on log could reveal remote names and local paths.

## Safe information for a bug report

Run these commands and review their output before attaching it:

~~~bash
rclone version
rclone config redacted
~~~

For one remote, a verbose read-only listing can also help:

~~~bash
rclone lsd 'Remote:' -vv
~~~

Include Plasma, KDE Frameworks, and rclone versions; whether the remote uses a
private OAuth client; the exact failed Dolphin action; the visible error; and
whether it also reproduces in `rclone lsd`, `rclone cat`, or `rclone rcat`.

Some systems forward KIO/Qt messages to the user journal:

~~~bash
journalctl --user -b --no-pager | rg -i 'kio|rclone'
~~~

Review that output privately: it may contain filenames or paths.

## Never share

Do not publish `rclone config show`, OAuth tokens, Google client secrets, an
unredacted configuration file, private paths, document names, or unrelated
file contents. `rclone config redacted` is a starting point, not a guarantee;
review it before posting.
