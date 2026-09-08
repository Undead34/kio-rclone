---
description: Prepare, publish, verify, and package a KIO Rclone release without mixing source and AUR state.
---

# Releasing

The source repository and the AUR recipe are released in sequence. Publish the
source tag first; update the AUR recipe only after that tag and its GitHub
workflow have succeeded.

## 1. Prepare the source release

Start from an up-to-date `main` branch and work on a release branch:

~~~bash
git switch main
git pull --ff-only
git switch -c release/X.Y.Z
~~~

Update the two machine-readable version values:

~~~bash
scripts/bump-version.sh X.Y.Z
~~~

Then add a dated `X.Y.Z` section to `CHANGELOG.md` and place the matching
`<release>` entry first in
`app/org.kde.kio-rclone-config.metainfo.xml`. Review the release notes as
user-facing text, not as a commit log.

Run the required local checks:

~~~bash
cmake -S . -B build/release -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DBUILD_TESTING=ON
cmake --build build/release
ctest --test-dir build/release --output-on-failure

pnpm --dir docs install --frozen-lockfile
pnpm --dir docs docs:build
~~~

Complete the relevant manual checks in [Testing](/testing). If a translation
catalog changed, [validate the installed language](/development#validate-an-installed-language)
before tagging.

## 2. Publish the source tag

Commit the release, fast-forward `main`, create an annotated tag, and push only
the intended branch and tag:

~~~bash
git switch main
git merge --ff-only release/X.Y.Z
git tag -a vX.Y.Z -m "Release X.Y.Z"
git push origin main vX.Y.Z
~~~

Use `git tag -s` instead of `git tag -a` when a signing key is configured. Do
not use `git push --tags`: it can publish unrelated local tags.

The `vX.Y.Z` push starts the release workflow. It verifies all in-repository
version values, builds the Arch package, runs its CTest gate, and creates the
GitHub Release only if those steps succeed.

## 3. Update the AUR recipe

Wait for the GitHub workflow to finish successfully. In the separate AUR
checkout, set `pkgver=X.Y.Z` in `PKGBUILD`, then regenerate the source hash and
metadata:

~~~bash
updpkgsums
makepkg --printsrcinfo > .SRCINFO
makepkg -f
pacman -Qp ./kio-rclone-*.pkg.tar.zst
~~~

Commit only `PKGBUILD` and `.SRCINFO`, then push the AUR repository:

~~~bash
git add PKGBUILD .SRCINFO
git commit -m "upgpkg: kio-rclone X.Y.Z-1"
git push origin master
~~~

Do not publish an AUR update if `makepkg -f` or its `check()` step fails.

## 4. Record the result

Record the source tag, package version, rclone/Plasma/KDE Frameworks versions,
and the manual test result. Keep tokens, client secrets, and unredacted rclone
configuration out of release notes and CI logs.
