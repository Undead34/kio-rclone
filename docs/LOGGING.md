# Logs and safe diagnostics

KIO Rclone should be debuggable without collecting private cloud data. It has
no telemetry service and does not create a second OAuth-token store: rclone
keeps the credentials in its own configuration.

## What the user sees today

- Dolphin receives readable KIO errors when rclone cannot list, read, write or
  authenticate.
- Upload notifications receive rclone's live percentage, rate, ETA and the
  finalization state.
- The package build keeps the full CTest result in the terminal or CI log.

No persistent KIO Rclone debug log is created by default. That is intentional:
an always-on log could reveal remote names and local paths.

## Safe information for a bug report

Run these commands and attach their output only after reviewing it:

~~~bash
rclone version
rclone config redacted
~~~

When the issue is limited to one remote, a verbose, read-only listing is also
useful:

~~~bash
rclone lsd 'Remote:' -vv
~~~

Also include:

- Plasma, KDE Frameworks and rclone versions;
- whether the remote uses a private OAuth client;
- the exact Dolphin action that failed;
- the visible error text and, if useful, a screenshot;
- whether the failure reproduces in `rclone lsd`, `rclone cat` or `rclone rcat`.

Some systems forward KIO/Qt process messages to the user journal. If yours
does, this can help correlate a failure with the desktop session:

~~~bash
journalctl --user -b --no-pager | rg -i 'kio|rclone'
~~~

Treat that output as private until reviewed: it may contain filenames or paths.

## Never share

Do not publish any of the following:

- `rclone config show` output;
- OAuth access tokens or refresh tokens;
- Google OAuth client secrets;
- an unredacted rclone configuration file;
- private paths, document names or file contents that are not relevant to the
  report.

`rclone config redacted` is the starting point, not an automatic guarantee;
review it before posting.

## Planned diagnostic mode

Before a wider distribution, add an explicit opt-in diagnostic mode with these
properties:

| Requirement | Rule |
| --- | --- |
| Activation | User-controlled, off by default |
| Destination | A user-selected log file or journal category |
| Redaction | Never log tokens, client secrets or authorization headers |
| Detail | Command category, elapsed time, error class and transfer state |
| Retention | No automatic upload; user owns deletion and sharing |
| Support bundle | Redacted config/version/test result only, after preview |

The implementation must be reviewed with real failure cases before declaring
the mode stable.
