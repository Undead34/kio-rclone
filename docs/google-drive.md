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
