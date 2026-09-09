---
description: Create your own Google Cloud OAuth client so Google Drive in KIO Rclone isn't limited by rclone's shared quota.
---

# Google Drive and your Google Cloud project

Using your own Client ID is the recommended way to connect Google Drive to KIO
Rclone. It avoids rclone's shared public quota and makes your API limit depend
on your project.

This guide is for a personal client used with your own account and remotes. It
is not a recipe for publishing an OAuth application to other users.

> [!TIP]
> rclone also maintains an [official guide to creating your own Drive Client
> ID](https://rclone.org/drive/#making-your-own-client-id). Use it if Google
> changes a console label; the concepts and values are the same.

## Before you start

You need a Google account, access to [Google Cloud
Console](https://console.cloud.google.com/), KIO Rclone installed, and rclone
working in your session. You do not need a web server, custom callback URL, or
service account. Create an OAuth client of type **Desktop app**.

## 1. Create a project and enable Drive API

1. Open [Create project](https://console.cloud.google.com/projectcreate).
2. Name it, for example `KIO Rclone personal`, and select it.
3. Open the [API library](https://console.cloud.google.com/apis/library/drive.googleapis.com)
   and click **Enable** for **Google Drive API**.

## 2. Configure Google Auth Platform

Open **Google Auth Platform** and complete **Branding**, **Audience**, and
**Data Access**. Add an app name, support email, and developer contact.

For a personal Gmail account choose **External** and add your own address as a
test user. Choose **Internal** only when every user belongs to your managed
Google Workspace organization. If requested, declare this Drive scope:

```text
https://www.googleapis.com/auth/drive
```

Do not add Gmail, Calendar, or other scopes: KIO Rclone does not need them.

## 3. Create OAuth credentials

1. Open **Google Auth Platform → Clients**.
2. Click **Create client**, choose **Desktop app**, and give it a name.
3. Copy the **Client ID** and **Client secret**, or download the credentials JSON.

Never commit the JSON or `rclone.conf`, and never paste credentials or tokens
into tickets or chats.

## 4. Connect it to KIO Rclone

Run `kio-rclone-config`, select your Google Drive remote, choose **Google
OAuth…**, and paste the Client ID and secret. Accept the dialog and complete
the browser flow opened by rclone. Then return to Dolphin and open `rclone:/`.

If offered **Import existing local OAuth override**, it migrates credentials;
rclone still stores and refreshes the token.

## 5. Verify safely

~~~bash
rclone lsd 'Google Drive:'
rclone config redacted
~~~

Then open `rclone:/Google%20Drive/` and test a disposable folder.

## Drive views in Dolphin

KIO Rclone recognizes rclone remotes reported as Google Drive and presents a
small hub at the remote root. The provider's ordinary root intentionally lives
under **My Drive**, so special Drive views can remain stable as the worker
grows:

| Dolphin location | What it represents | Changes |
| --- | --- | --- |
| `rclone:/Google%20Drive/my-drive/` | Your normal Drive root | Allowed when Drive permits it. |
| `rclone:/Google%20Drive/shared-with-me/` | Files shared with you | Read-only. |
| `rclone:/Google%20Drive/shared-drives/` | Shared Drives available to the account | Open a named drive; its URL uses the stable Drive ID. |
| `rclone:/Google%20Drive/trash/` | Trashed items in their original folder structure | Read-only. |
| `rclone:/Google%20Drive/starred/` | Starred files | Read-only. |
| `rclone:/Google%20Drive/folders/<FOLDER_ID>/` | A known folder ID as a temporary root | Allowed when Drive permits it. |

The `folders/<FOLDER_ID>/` route is intentionally entered in Dolphin's
location bar; it is not listed as an empty folder because rclone cannot
enumerate every useful folder ID. It is useful for a folder URL or an ID you
already know, including a supported Computer folder.

**Trash is not a flat Google Drive web view.** rclone deliberately preserves
each trashed item's original path, so Dolphin can show ordinary parent folders
that only provide navigation to a trashed child. Those folders are contextual
scaffolding, not a second listing of your live Drive. The worker labels the
view **Trash (original folder structure)** and keeps it read-only rather than
inventing an ID-based flat view that could not safely support normal KIO file
operations.

If you already bookmarked a raw Drive path, insert `my-drive/`: for example,
`rclone:/Google%20Drive/Projects/` becomes
`rclone:/Google%20Drive/my-drive/Projects/`. The worker does not guess which
virtual view an old path meant, because guessing would make real folders named
like a special view ambiguous.

KIO Rclone delegates these views to rclone connection strings and `backend
drives`; it does not store another Google account database or call the Google
API directly. The exact flags and backend command are documented by
[rclone's Google Drive backend](https://rclone.org/drive/).

Filtered views stay read-only on purpose: a file being shared, starred, or in
the Trash is not proof that editing it through that filtered path is safe.
Restore Trash entries explicitly with rclone if needed; restoration is not yet
a Dolphin action. Moving between two Drive views is also left to KIO's normal
copy workflow rather than guessing whether the two virtual roots are the same
provider folder. Duplicate visible file names remain conservative and
read-only until their identity can be represented safely in a public KIO URL.

## Testing mode and seven-day tokens

External apps in **Testing** are limited to their test users. For Drive
scopes, authorization and refresh tokens for test users expire after seven
days. If you are asked to log in weekly, check the project's publishing state.

For a strictly personal Client ID, rclone documents publishing an app without
verification and accepting Google's unverified-app warning. Do this only for
your own accounts; public or multi-user apps must follow Google's current
verification requirements.

## Common errors

| Error | Likely cause | Action |
| --- | --- | --- |
| `access_denied` | Account is not a test user or a policy blocks the app. | Add the account or contact the Workspace administrator. |
| `org_internal` | An Internal client is used outside its organization. | Use an organization account or an External client. |
| `invalid_client` | Credentials are wrong or from another project. | Copy them again and reconnect the remote. |
| `RATE_LIMIT_EXCEEDED` | Shared Client ID or project quota. | Use your Client ID and check Cloud Console quota. |

## Google Workspace and managed accounts

In a company or school, the Client ID can be valid and still fail:

- **Internal** only permits users in the corresponding organization.
- An administrator may block external OAuth apps.
- API policy may restrict the Drive scope.
- Google Admin may need to approve the Client ID.

Share the Client ID and exact requested scope with the administrator; do not
share tokens or your Client secret.

## Minimum security

- Create a project exclusively for this use.
- Never commit the credentials JSON.
- Do not share the Client secret, refresh token, or unredacted `rclone.conf`.
- Use `rclone config redacted` when asking for help.
- If the computer is compromised, revoke access from your Google account and
  authorize the remote again.

See [Getting started](/getting-started) or [Troubleshooting](/troubleshooting)
when you are done.
