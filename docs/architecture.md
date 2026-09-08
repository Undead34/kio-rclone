---
description: Boundaries, invariants, and KDE integration contracts for maintainers of KIO Rclone.
---

# Architecture

This page is the maintainer map for KIO Rclone. It describes the boundaries
that keep a KIO protocol worker small and predictable; it is not a second user
manual or a replacement for the API documentation linked below.

## Directory map

The folder is part of the architecture: a maintainer should be able to locate
the responsible layer before opening a file.

```text
src/
  cache/     directory-listing policy and persistent snapshots
  rclone/    CLI client, process setup, URL parsing, item presentation
  kio/       KIO worker, UDS entry building, KDirNotify boundary
app/
  dialogs/   settings and interactive rclone prompts
  configwindow.cpp  remote-management window and UI orchestration
```

Do not add catch-all folders such as `helpers`, `common`, or `utils`. A new
folder needs a dependency boundary that can be stated in one sentence.

## One sentence per component

| Component | Owns | Must not own |
| --- | --- | --- |
| `RcloneWorker` | KIO request/response adaptation, progress, KIO-visible errors, and applying a listing policy to a request. | rclone protocol parsing, cache serialization, or preference persistence. |
| `RcloneUrl` | Parsing and constructing `rclone:/` URLs. | Process invocation or KIO entries. |
| `RcloneClient` | rclone command arguments, process results, and rclone JSON decoding. | `KIO::WorkerResult`, `UDSEntry`, or DBus notification. |
| `DirectoryListingPolicyStore` | Persisting and normalizing the user's freshness choice. | Snapshot bytes or KIO request metadata. |
| `DirectorySnapshotCache` | Short-lived, private directory snapshots. | Deciding whether a request is a reload or reading user preferences. |
| `RcloneEntryFormat` | Provider icon, MIME fallback, version comparison, and duplicate preference. | Publishing entries to a KIO client. |
| `KioEntryBuilder` / `KioDirectoryNotifier` | The narrow `UDSEntry` and KDirNotify boundaries. | rclone CLI calls or cache policy. |
| Configuration application | Widgets and interactive configuration flow. | Worker request handling. |

The intended direction of dependencies is:

```text
KDE / Dolphin
    │
    ▼
RcloneWorker ──► RcloneClient ──► rclone
    │
    ├──► DirectoryListingPolicyStore
    ├──► DirectorySnapshotCache
    └──► KioEntryBuilder / KioDirectoryNotifier
    ▼
KDE / Dolphin
```

`RcloneWorker` is deliberately the only place where a normal remote operation
should need to know about both KIO and rclone. New code should preserve that
boundary rather than adding another cross-cutting helper to the worker.

## Operational contracts

### Directory listing

1. Parse the KIO URL once into a canonical remote and remote path.
2. Honour an explicit reload request before considering a snapshot.
3. Read the current `DirectoryListingPolicy` and load a snapshot only when it
   explicitly allows it.
4. Otherwise request the directory from rclone and make its items safe for
   KIO representation.
5. Persist a snapshot only after a complete, successful list operation.
6. Publish KIO entries only through the KIO entry mapper.

An incomplete, cancelled, or failed rclone listing is never a cache entry.
This is a correctness rule, not merely an optimisation choice.

The worker exposes this sequence as small, use-case-sized methods:

```text
listDir
  ├── listRoot
  ├── cachedDirectory → publishDirectoryEntries
  └── listRemoteDirectory
       ├── listUniqueDirectory → stream entries → snapshot store
       └── listDuplicateSafeDirectory → publishDirectoryEntries → snapshot store
```

`F5`/reload enters only through the explicit reload branch. Normal navigation
continues to be eligible for a fresh snapshot, so cache invalidation cannot
accidentally change ordinary enter/leave-folder behavior.

### Mutation

For `put`, `mkdir`, `rename`, `copy`, and `del`, successful remote work has a
single follow-up contract:

```text
remote operation succeeded
    ├── invalidate every affected directory snapshot
    └── notify KIO of the corresponding visible change
```

Metadata from a snapshot may improve `stat` and MIME discovery, but it is not
sufficient authority to mutate a remote object. A mutation must resolve or
validate the relevant target first.

### Duplicate names and unrepresentable paths

KIO represents a listed item through one `UDS_NAME`, which becomes a URL path
component. Empty names, `.`/`..`, slash-containing names, and NUL-containing
names cannot be published as ordinary children. `KioEntryBuilder::isRepresentable`
keeps that validation at the KIO mapping boundary; listing paths call it before
they stream or accumulate an entry.

If a remote can expose two items with the same visible name, select one
read-only representative before publishing it. Never silently perform a
write against an ambiguous path.

## KDE contracts worth knowing

These links are intentionally concentrated here. Link a specific API from a
code comment only when the local invariant would otherwise be surprising.

| API | Project rule | Reference |
| --- | --- | --- |
| `KIO::WorkerBase` | The worker adapts protocol requests and has no persistent event loop. Do not put long-lived watchers or background services in it. | [WorkerBase](https://api.kde.org/kio-workerbase.html) |
| `KIO::UDSEntry` | Construct a complete, representable entry at the KIO boundary. In particular, `UDS_NAME` is a path component, not arbitrary remote metadata. | [UDSEntry](https://api.kde.org/kio-udsentry.html) |
| `KIO::WorkerResult` | Translate rclone failures into a KIO error once, at the worker boundary. Do not leak rclone error strings as a control-flow API. | [WorkerResult](https://api.kde.org/kio-workerresult.html) |
| `KDirNotify` | After a successful mutation, notify KIO only after invalidating affected local snapshots. | [KDirNotify interface](https://api.kde.org/legacy/4.12-api/kdelibs-apidocs/kio/html/classOrgKdeKDirNotifyInterface.html) |
| `KCoreDirLister` | Dolphin/KIO may reuse a session listing. That cache is separate from KIO Rclone's persistent snapshot cache and must not be relied on for correctness. | [KCoreDirLister](https://api.kde.org/kcoredirlister.html) |
| `KSharedDataCache` | Persistent cache data is best-effort and evictable. It is never an authoritative remote index or a home for credentials. | [KCoreAddons API](https://api.kde.org/kcoreaddons-module.html) |

## Where new code belongs

| Change | Preferred home |
| --- | --- |
| Another rclone JSON field or command flag | `src/rclone/` and its tests. |
| A different KIO error mapping or progress signal | `RcloneWorker` boundary code. |
| Directory freshness, deduplication, or streaming choice | The directory-listing operation, with a policy-focused test. |
| Policy persistence or its range | `DirectoryListingPolicyStore`. |
| Snapshot serialization, size limit, expiry, or migration | `DirectorySnapshotCache`. |
| Mapping `RcloneItem` to `UDSEntry` | `KioEntryBuilder`. |
| A successful create/rename/delete side effect | A mutation coordinator: cache invalidation and notification together. |
| OAuth dialog or provider-specific configuration UI | `app/` or a focused `app/dialogs/` component, outside the worker. |

Do not introduce an abstract interface just to give a class a pattern name.
Introduce a seam when it separates an external boundary, isolates a stateful
workflow, or lets a test replace a real dependency.

## Deferred KIO capabilities

The worker header intentionally declares only supported KIO operations. These
are future capabilities, not near-term TODO comments sprinkled through the
public class:

- connection lifecycle, only if rclone gains a worker-owned persistent session;
- permissions and modification time, subject to backend metadata support;
- Drive shortcuts, gated to the backend that supports them;
- `KIO::FileJob` random access (`open`, `read`, `write`, `seek`, `truncate`,
  `close`), after range streaming and local staging have characterization
  tests;
- `special()`, only for a command that cannot be represented by standard KIO
  operations.

Each future capability starts with its observable contract and a focused test,
then earns a place in the worker. A clean seam with no behavioral change is
more valuable than a large, mixed cleanup commit.

## Tests as contracts

Tests should target externally observable guarantees rather than private
method names:

- list cache: fresh hit, explicit reload, failed/cancelled list, configuration
  change, and size limit;
- KIO entry mapping: representable names, MIME/type/access metadata, and
  duplicate items;
- mutation: the remote action, cache invalidation, and matching KIO
  notification form one successful transaction;
- backend: rclone JSON, process failure, cancellation, and command arguments.

When an implementation moves, keep these tests stable. If a test must change,
it should be because the user-visible contract intentionally changed.
