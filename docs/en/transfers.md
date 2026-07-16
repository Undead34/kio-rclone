# Transfers, pause, and progress

~~~text
local file ⇄ KIO ⇄ KIO Rclone ⇄ rclone ⇄ provider
~~~

## Uploads

KIO Rclone starts `rclone rcat` with a temporary remote name and feeds it data
as rclone accepts it. The final name is untouched until the complete upload is
validated; then `rclone moveto` publishes it. Cancellation or failure removes
the temporary object and preserves the previous version.

| Status | Meaning |
| --- | --- |
| Preparing upload… | rclone is creating the operation and resolving the remote. |
| Uploading… 42% · 8.1 MiB/s | Live statistics reported by the backend. |
| Finalizing upload… | All local bytes reached rclone; the provider is confirming the last block. |
| Committing upload… | The complete object is moving to its final name. |
| Completed | rclone exited successfully and KIO confirmed the job. |

> [!IMPORTANT]
> Dolphin's main bar can reach 100% before finalization finishes. Wait for the
> notification to complete successfully.

## Downloads

Known-size unique files stream to KIO in chunks. Pause applies backpressure to
the rclone process and cancel terminates it. Unknown-size Google exports and
duplicate names are first materialized locally so KIOFuse and LibreOffice get
a real size and one complete file.

## Performance

Provider OAuth quota, backend latency/limits, and chunk size usually matter
more than the worker. Google Drive uses 8 MiB chunks by default.

For an honest measurement, compare the same file, network, remote, and Client
ID. Do not compare a transfer under shared quota with one under a private OAuth
project.

## What to try if something seems wrong

- Pause a large download: network activity should settle.
- Resume it: the copy should continue and finish without duplicating the file.
- Upload a multi-chunk file and watch the percentage, speed, and
  **Finalizing upload…** status.
- Cancel it: the remote file should not appear as a completed copy.
- Press F5 when done: Dolphin should request the remote listing again.

If it fails, follow the [safe diagnostics guide](/en/LOGGING).
