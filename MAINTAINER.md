# Maintainer workflow

This file is a local repository guide. It is not part of the VitePress site.

## Protected branches

`main` is the released, stable line. Do not commit or push to it directly.
GitHub requires a pull request, linear history, and resolved review
conversations; it also rejects force-pushes and branch deletion. The repository
administrator may bypass the rule for recovery.

`develop` is the integration branch. Put normal work and its commits there.

~~~bash
git switch develop
git pull --ff-only
# edit, build, test
git add <files>
git commit -m "..."
git push origin develop
~~~

## Prepare a release

Create a short-lived branch from the current integration line:

~~~bash
git switch develop
git pull --ff-only
git switch -c release/X.Y.Z
~~~

Update the version, changelog, AppStream release entry, and all required
checks. Push the branch and open a pull request to `main`:

~~~bash
git push -u origin release/X.Y.Z
gh pr create --base main --head release/X.Y.Z \
  --title "Release X.Y.Z" --fill
~~~

Merge the pull request with **Rebase and merge** so `main` stays linear. Then
tag the exact commit that reached `main`:

~~~bash
git switch main
git pull --ff-only
git tag -a vX.Y.Z -m "Release X.Y.Z"
git push origin vX.Y.Z
~~~

The tag starts the GitHub release workflow. Update the AUR only after that
workflow succeeds.

Finally, align `develop` with the released history before starting more work:

~~~bash
git switch develop
git pull --ff-only
git rebase origin/main
git push --force-with-lease origin develop
~~~

`develop` is intentionally not protected, so the safe lease-protected update
is allowed. Never force-push `main`.

## Inspect or change the GitHub rule

View the active rule:

~~~bash
gh api repos/Undead34/kio-rclone/branches/main/protection
~~~

The current rule requires pull requests but zero approving reviews, which fits
a sole maintainer. If collaborators join, require one approval through the
GitHub repository settings or update the protection API request accordingly.
