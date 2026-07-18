# Distribution and release plan

This is a staged plan for shipping KIO Rclone without pretending it is a
finished cross-distribution product today. Each stage has a concrete release
gate.

## Principles

- Keep rclone as the provider and credential authority.
- Ship a native KIO plugin through the host distribution, not a sandbox that
  cannot install a worker into the host's Qt plugin path.
- Make every package reproducible, reviewable and signed by its distributor.
- Never require an account, telemetry or a background daemon merely to browse a
  remote.
- Prefer reliable release gates over a large set of untested package targets.

## Phase 0 — Release foundation

Before public packaging work:

- [ ] Add a top-level `LICENSE` file matching the SPDX headers.
- [ ] Add AppStream metainfo so software centers can describe the utility.
- [x] Define a changelog and semantic-versioning policy.
- [ ] Choose the canonical public forge and configure protected release tags.
- [ ] Add CI for CMake, CTest and the Arch package `check()` step.
- [x] Build the VitePress site in CI and publish it to GitHub Pages.
- [x] Select GitHub Pages as the first static host.
- [ ] Add a release issue template containing the manual test matrix.
- [ ] Verify a clean source archive builds without ignored local artifacts.

Exit gate: a tagged source tree builds, tests and packages in a clean Arch
environment with no cloud credentials, and the user documentation builds to
static HTML.

### Documentation hosting

The VitePress site is deliberately independent from the KIO package. The
`Deploy documentation` workflow builds `docs/.vitepress/dist/` and deploys it
to `https://undead34.github.io/kio-rclone/` after a push to `main`. There is no
server-side application to operate.

The workflow accepts only trusted `main` pushes or a manual dispatch; it does
not run on pull requests, uses no repository secrets, grants write access only
to the deployment job, and pins every GitHub Action to a full commit SHA.
Other static hosts can use the same `pnpm install --frozen-lockfile` and
`pnpm docs:build` commands, adjusting `DOCS_BASE` if the site is served under a
subdirectory.

## Phase 1 — Arch Linux first

The Arch recipe lives in its own AUR repository, separate from this source
tree, so the PKGBUILD is not part of the tarball it checksums. The first public
target is that AUR source package, after checking that the package name is
available and that the PKGBUILD passes namcap/review.

Release workflow:

1. Update version, changelog and release notes; commit.
2. Tag the exact source commit and push; the release workflow builds, tests
   and publishes the GitHub Release.
3. In the AUR repository: bump `pkgver`, run `updpkgsums` (fills the real
   source hash), regenerate `.SRCINFO`, and push. `makepkg -f` must build and
   pass `check()`.
4. Confirm a fresh Arch user can install the recipe and complete the smoke
   test; do not present it as an official Arch binary repository yet.

Exit gate: a fresh Arch user can install from the package recipe, open
`rclone:/`, configure a remote and complete the smoke test.

## Phase 2 — Quality, support and signing

Before asking other distributions to carry it:

- [ ] Run the manual matrix in [testing.md](testing.md) for each release.
- [ ] Test private Google OAuth and one non-Google rclone backend.
- [ ] Publish known limitations and upgrade notes.
- [ ] Define a security-contact and responsible-disclosure path.
- [ ] Sign release tags and publish checksums for artifacts.
- [ ] Add an opt-in, redacted diagnostic mode described in [logging.md](logging.md).
- [ ] Track crash reports and regressions without uploading user metadata.

Exit gate: regressions can be reproduced from a redacted report and a release
artifact can be traced to a signed source tag.

## Phase 3 — Additional distributions

Package only where there is an owner willing to test it in that distribution's
KDE environment.

| Target | Delivery route | Prerequisite |
| --- | --- | --- |
| Fedora | RPM/COPR first, then a Fedora review | Current KDE Frameworks and a maintainer/tester |
| openSUSE | OBS home project first | Plasma/KIO smoke testing on Tumbleweed |
| Debian/Ubuntu | Debian packaging or a maintained PPA | Stable dependency mapping and maintainer review |
| KDE-oriented distributions | Native repository review | Respect the distributor's KDE plugin policy |

Do not start with Flatpak: a KIO worker must be installed into the host KDE/Qt
plugin environment, which a normal application sandbox does not provide.

## Phase 4 — Upstream discussion

After the standalone package has stable users and a clear maintenance owner,
evaluate an upstream KDE discussion. The proposal should include:

- rationale for using rclone rather than KDE Online Accounts;
- security and credential-boundary design;
- code-quality, translation and CI status;
- ownership for bug triage and releases;
- evidence from the manual compatibility matrix.

Upstreaming is a maintenance commitment, not merely another distribution
channel.

## Release gates

Every public version should satisfy all of these:

| Gate | Required evidence |
| --- | --- |
| Source hygiene | Clean tree, SPDX headers, license and release notes |
| Build | Clean CMake/Ninja build on the target platform |
| Automated tests | Full `ctest --output-on-failure` pass |
| Package | Package build and its `check()` pass |
| Manual KDE test | Browse, upload, download, pause, cancel and refresh |
| Provider test | Private Google OAuth plus one other backend |
| Diagnostics | Redacted failure reporting documented and reviewed |
| Security | No token, secret or personal file included in artifacts/logs |

## First public milestone

The practical first milestone is **0.3.x on Arch**:

1. Finish Phase 0.
2. Recruit a few testers with independent rclone remotes.
3. Collect only redacted reports and reproducible steps.
4. Fix transfer, OAuth and Dolphin integration regressions.
5. Promote the validated PKGBUILD to AUR.

Only after that should a binary repository or additional distribution be
considered.
