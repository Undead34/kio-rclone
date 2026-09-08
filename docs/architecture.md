---
description: Boundaries, invariants, and KDE integration contracts for maintainers of KIO Rclone.
---

# Architecture

This page is the maintainer map for KIO Rclone. It describes the boundaries
that keep a KIO protocol worker small and predictable; it is not a second user
manual or a replacement for the API documentation linked below.

## One sentence per component

| Component | Owns | Must not own |
| --- | --- | --- |
| `RcloneWorker` | KIO request/response adaptation, progress, and KIO-visible errors. | rclone protocol parsing or cache policy details. |
| `RcloneUrl` | Parsing and constructing `rclone:/` URLs. | Process invocation or KIO entries. |
| `RcloneClient` | rclone command arguments, process results, and rclone JSON decoding. | `KIO::WorkerResult`, `UDSEntry`, or DBus notification. |
| `DirectorySnapshotCache` | Short-lived, private directory snapshots. | Deciding whether a request is a reload or safe to serve from cache. |
| `RcloneEntryFormat` | Provider icon, MIME fallback, version comparison, and duplicate preference. | Publishing entries to a KIO client. |
| Configuration application | Widgets and interactive configuration flow. | Worker request handling. |

The intended direction of dependencies is:

```text
KDE / Dolphin
    │
    ▼
RcloneWorker ──► operation policy ──► RcloneClient ──► rclone
    │                    │
    │                    └──────────► DirectorySnapshotCache
    ▼
UDSEntry / WorkerResult / KDirNotify
```

`RcloneWorker` is deliberately the only place where a normal remote operation
should need to know about both KIO and rclone. New code should preserve that
boundary rather than adding another cross-cutting helper to the worker.

## Operational contracts

### Directory listing

1. Parse the KIO URL once into a canonical remote and remote path.
2. Honour an explicit reload request before considering a snapshot.
3. Load a fresh snapshot only for normal, read-only discovery.
4. Otherwise request the directory from rclone and make its items safe for
   KIO representation.
5. Persist a snapshot only after a complete, successful list operation.
6. Publish KIO entries only through the KIO entry mapper.

An incomplete, cancelled, or failed rclone listing is never a cache entry.
This is a correctness rule, not merely an optimisation choice.

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
names cannot be published as ordinary children. Keep that validation next to
the KIO entry mapping, not spread across list, stat, and mutation code.

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
| Another rclone JSON field or command flag | `RcloneClient` and its tests. |
| A different KIO error mapping or progress signal | `RcloneWorker` boundary code. |
| Directory freshness, deduplication, or streaming choice | The directory-listing operation, with a policy-focused test. |
| Snapshot serialization, size limit, expiry, or migration | `DirectorySnapshotCache`. |
| Mapping `RcloneItem` to `UDSEntry` | The KIO entry mapper/formatter. |
| A successful create/rename/delete side effect | A mutation coordinator: cache invalidation and notification together. |
| OAuth dialog or provider-specific configuration UI | The configuration application, outside the worker. |

Do not introduce an abstract interface just to give a class a pattern name.
Introduce a seam when it separates an external boundary, isolates a stateful
workflow, or lets a test replace a real dependency.

## Refactoring sequence

The project should evolve in small, behaviour-preserving commits:

1. Keep this document and narrow code comments as the architecture contract.
2. Move `UDSEntry` construction and KIO notifications out of the worker into
   focused KIO-boundary helpers.
3. Extract directory listing, snapshot policy, and duplicate handling into one
   operation service while preserving existing cache/reload behaviour.
4. Extract upload/download and mutation workflows only after characterization
   tests cover the current behaviour.
5. Refactor the configuration UI separately from the worker.

Each extraction should retain the same public KIO behaviour and add a focused
test for the contract it makes explicit. A clean seam with no behavioural
change is more valuable than a large, mixed cleanup commit.

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
