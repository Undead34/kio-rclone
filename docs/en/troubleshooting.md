# Troubleshooting

## Google Drive is slow before bytes move

The shared rclone Client ID may be quota-limited. Configure [your own Client
ID](/en/google-drive), authorize the remote again, and retry.

## Access denied, invalid_grant, or repeated login

Run `kio-rclone-config`, select the remote, and choose **Reconnect…**. For
Google Drive, verify that the Client ID and secret match the project used for
the token. If the project is in **Testing**, check the seven-day token limit
in the [Google Drive guide](/en/google-drive#testing-mode-and-seven-day-tokens).

## The bar says 100% but the job is still running

Wait for **Finalizing upload…** and **Committing upload…**. The provider may
still be confirming or publishing the final object.

## LibreOffice opens an empty or corrupt document

Use KIO Rclone 0.3.0 or later. Native Google exports and duplicate names are
opened read-only after local materialization. Resolve duplicates in
Drive/rclone before editing.

Native Google documents exported as DOCX/XLSX/PPTX also open read-only. Save a
copy under another name if you want to turn one into an ordinary Office file;
automatic re-import could replace the original collaborative document.

## A TXT or other ordinary file does not keep changes

In 0.3.0, saving publishes from a temporary remote name. A cancellation,
network failure, or another modification during flush preserves the complete
remote version instead of leaving partial bytes.

If it still fails, check that the backend supports `rcat` and `moveto`, that
there is no other object with the same name, and that no other application
changed the file during upload.

## `rclone:/` does not appear

1. Confirm installation with `pacman -Q kio-rclone`.
2. Run `kbuildsycoca6 --noincremental`.
3. Restart Dolphin.
4. Confirm that `rclone version` works.

## It fails only at a company or Google Workspace account

An administrator may restrict external OAuth apps, Drive scopes, or internal
apps. See the Workspace section in [Google Drive and GCP](/en/google-drive).

For help, share versions, redacted configuration, exact steps, and the visible
error—never tokens or client secrets.
