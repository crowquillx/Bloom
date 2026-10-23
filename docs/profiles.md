# Bloom profiles

Bloom profiles ("spaces" in the UI) are Bloom-owned groups of server connections. A user switches between them from the navigation rail, and a merged space browses several connections at once. They are distinct from provider household profiles (for example Silo or Jellyfin user profiles).

Issue #91 defines the feature. This page covers the schema, the repository, session switching and routing, merged reads, and the UI.

## Terminology

| Term | Meaning |
| --- | --- |
| **Connection** | One `ServerConnection` identity: server + account + provider profile + credential reference. |
| **Provider profile** | Server-owned user/household profile selected within a connection. |
| **Bloom profile** | User-named, ordered set of independent connection memberships. |
| **Membership** | One connection attached to a Bloom profile (`memberId` + `connectionId`). |
| **Single mode** | Exactly one membership; preserves today’s single-connection browsing behavior. |
| **Merged mode** | Multiple memberships browsed together through `MergedCatalogService`. |
| **Live session** | The one connection `AuthenticationService` is signed in to. Details, playback, and writes run on it. |

## Identity rules

- `connectionId` is one server + account + provider-profile credential identity. The same physical server may appear as multiple connections when users/accounts differ; those connections have distinct `connectionId`s.
- Within one Bloom profile, at most one membership per `connectionId` so `MediaRef{connectionId,itemId}` stays unambiguous.
- Never dedupe or key memberships by `serverId`, `baseUrl`, or account alone.
- Membership identity belongs in request/source context, not in `MediaRef` or cache keys. `memberId` must not be copied into media identity or disk caches.
- Profile JSON stores only Bloom profile fields and `connection_id` references. No URLs, accounts, provider profile IDs, usernames, or credentials.

## Persistence (`settings.bloom_profiles`)

Config app version 30 introduces schema version 1:

```json
{
  "version": 1,
  "active_profile_id": "…",
  "items": [
    {
      "id": "…",
      "name": "Default",
      "mode": "single",
      "members": [
        {
          "member_id": "…",
          "connection_id": "…",
          "enabled": true,
          "priority": 0,
          "label_override": "optional"
        }
      ],
      "default_member_id": "…",
      "created_at": "2026-07-21T18:00:00Z",
      "updated_at": "2026-07-21T18:00:00Z"
    }
  ]
}
```

- Member order is the ordered `members` array; `priority` is normalized to `0..n-1` deterministically.
- `ConfigManager::getBloomProfilesConfig` / `setBloomProfilesConfig` are raw read/write accessors (setter saves). Validation and CRUD live in `BloomProfileRepository`.

## Migration (config v29 → v30)

- Preserve prior single-connection behavior by creating **one** default `single` Bloom profile containing **only** the current active connection, or the sole saved connection when none is active.
- Do **not** put every saved connection into the migrated profile.
- Zero connections → empty v1 block (`active_profile_id` empty, `items` empty).
- Profile IDs use deterministic UUIDv5 values derived from `connectionId`; member IDs derive from the Bloom profile plus `connectionId`, so retries are idempotent without conflating memberships across profiles.
- `settings.connections.active` and existing connection behavior stay unchanged in this slice.
- Default config ships an empty v1 block.

## Repository API

`BloomProfileRepository` (`src/profiles/BloomProfileRepository.*`) is a provider-neutral `QObject` registered in `ServiceLocator` after `ConfigManager` (production and test).

Public surface:

- `profiles()`, `activeProfile()`, `profile(id)`
- `upsertProfile`, `removeProfile`, `setActiveProfile`, `reorderProfiles` (`upsertProfile` takes a reference and writes back assigned IDs/timestamps; `reorderProfiles` changes picker order without bumping the generation)
- `addMember`, `removeMember`, `reorderMembers`, `setDefaultMember`
- Immutable `BloomProfileRequestContext` snapshots: `bloomProfileId`, `memberId`, `connectionId`, `generation`
- `generation()`, `activeRequestContext()`, `requestContext(profileId, memberId)`, `isCurrent(context)`

### Validation and repair

On load (and when connections change), the repository:

- Drops members whose `connectionId` is missing from `ConfigManager::getConnections`
- Dedupes only identical `connectionId` (keeps the first)
- Regenerates empty member IDs deterministically from the Bloom profile plus `connectionId`; regenerates duplicate member IDs with a new UUID
- Repairs `default_member_id` to the first enabled member, else the first member
- Repairs `active_profile_id` to the first valid profile when needed
- Drops empty profiles; an empty repository is valid
- Persists repairs idempotently

**Single mode:** must resolve through exactly one membership. If an upsert supplies more than one member, the repository deterministically retains the default member when present, otherwise the first, and drops the rest. `addMember` rejects a second membership in Single mode.

**Merged mode:** may include multiple distinct `connectionId`s.

### Request context epoch

Switching the active profile, default member, or membership-affecting mutations increments `generation`. Callers keep a `BloomProfileRequestContext` and use `isCurrent` to ignore stale async results.

The repository never changes `ConfigManager`’s active connection. `WorkspaceController` does that explicitly through `AuthenticationService` (see below).

## Types

- `BloomProfileMode { Single, Merged }`
- `BloomProfileMember` — `memberId`, `connectionId`, `enabled`, `priority`, optional `labelOverride`
- `BloomProfile` — stable UUID `id`, `name`, `mode`, ordered `members`, `defaultMemberId`, timestamps
- `BloomProfileRequestContext` — profile/member/connection + generation

Snake_case JSON; C++ API uses camelCase fields.

## Sessions, switching, and routing

`AuthenticationService` keeps exactly one live, refreshable session. `WorkspaceController` (`src/profiles/WorkspaceController.*`, QML `Workspaces`) keeps that session on an enabled member of the active Bloom profile.

- **Switching** a space selects it in the repository, then activates its default member through `AuthenticationService::activateConnection`. If that member cannot sign in, the controller tries the other enabled members in order. If every member fails, the previous space and session come back and `switchFailed` reports the error. `workspaceSwitched` tells Main to replace the page stack with a fresh Home.
- **Activation** reads the saved connection's tokens from `CredentialStore` off the UI thread, validates them, and refreshes them when the provider supports it. An explicit activation never deletes credentials, because the server may only be unreachable. A failed activation falls back to the session it replaced. A provider profile or PIN prompt hands the half-restored session to the login screen (`signInRequired(connectionId)`). A space switch then leaves the switching state and finishes (`workspaceSwitched`) once the login screen signs that member in; a route fails instead, because its pending action cannot wait for interactive input. Only the newest restoration clears `isRestoringSession`, so a fallback or next-member activation started from a failure keeps it set.
- **Routing.** Merged items keep their owning `connectionId`. Before details, playback, or a watched/favorite write, QML calls `Workspaces.routeTo(connectionId)` through `SourceRouter.qml`. When another member owns the item, the live session moves to that member and the action runs on `routeReady`. Routing does not change the space, so Home and Search keep their merged results. Connection-bound pages (`LibraryScreen`, `UpNextScreen`, `ActiveSessionsScreen`) declare `ownerConnectionId`, which Main stamps with the live connection when the page is first shown. Back skips pages whose owner is no longer live, so a page left behind by a route never runs actions through another account. The rail matches the open library by `connectionId` as well as library ID. Connections outside the active space are refused. Autoplay and Next Up therefore stay on the owning connection.
- **Adding a connection.** `beginAddConnection(targetProfileId)` leaves the live session without deleting its credentials and shows the login screen. Signing in adds the new connection to the target space, which becomes merged if it was single. An empty target creates a new single space. `cancelAddConnection` restores the previous session.
- **Plain sign-in** (startup restore or the login screen after sign-out) keeps the active space when it contains the account. Otherwise Bloom selects the first space that does, or creates a single one.
- **Removing a connection** (`removeConnection`) deletes that connection's credentials and metadata (`ConfigManager::removeConnection`). Connection-scoped preferences stay under `connection_state`, so signing the same account in again restores them. The repository drops its memberships and any space left empty. Other members, including another user on the same server, are untouched. Removing the live connection signs it out and continues in the next space.
- **Sign-out** from the picker keeps the previous behavior: it deletes the live connection's credentials and keeps its metadata.

## Merged reads

`MergedCatalogService` (`src/profiles/MergedCatalogService.*`, QML `MergedCatalog`) is active only while the active profile is merged and has an enabled member. Single spaces keep using `LibraryService` unchanged.

- **Credentials.** `ConnectionSessionPool` (owned by `AuthenticationService`) loads every member's access/profile tokens off the UI thread and holds them in memory only. `AuthenticationService::requestContextForConnection` serves the live session's context for the live connection and pooled contexts for the others. Pooled tokens are never refreshed or written. Signing out, or otherwise deleting a connection's credentials, also forgets its pooled entry, including a pending load. After a `401`/`403`, the pool reloads the tokens from storage once, because the live session may have rotated them. If the request is still rejected, the source reports `signInRequired`. A rejected member never expires the live session.
- **Transport.** Merged reads use a private `HttpTransport` over the shared access manager. Session switches cancel `AuthenticationService`'s transport but not merged reads. Each request is built by the member's own provider request factory, so tokens never cross connections.
- **Operations.** Views (Home and the rail), Next Up, Recently Added per library (owning member only), and Search (Movie/Series). Each source is bounded by the provider's request limit. Merged results are not paginated. A library opens on its owning connection, where `LibraryViewModel` pages through that source's own cursors.
- **Partial failure.** Each member reports `loading`, `ready`, `failed`, or `signInRequired` in `MergedCatalog.sources`. A failed member contributes nothing and never blanks healthy results. Main shows one toast naming the unavailable sources. A `*Failed` signal fires only when every source fails.
- **Staleness.** Any membership, mode, or active-profile change starts a new epoch. It cancels in-flight merged requests, and results from the old epoch are dropped. An open Search page clears its results and searches the new source list again (`MergedCatalog.activeChanged`).
- **Artwork.** `ActiveArtworkProvider` picks the provider from the artwork's own connection. Jellyfin and Silo artwork requests go through `createRequestForConnection`, so pooled members load posters with their own server and token. Artwork cache keys were already connection-scoped and contain no credentials.
- **Caches.** Merged reads write no disk cache. Library SQLite, detail, and track-preference caches stay keyed by connection scope, so a routed switch reopens the owning connection's caches instead of sharing another member's data.

### Merge policy

`IMergedCatalogPolicy` (`src/profiles/MergedCatalogPolicy.*`) is the extension point for later policies such as deduplication, source preference, failover, or per-user visibility. Those need no schema change. `DefaultMergedCatalogPolicy` implements the MVP rules:

- No cross-server deduplication or progress reconciliation. The same title from two members appears twice, each with its own `MediaRef`.
- Views: member priority, then provider order.
- Next Up: most recent `userState.lastPlayedAt` first, then member priority, then provider order, then `MediaRef`.
- Search: relevance is not comparable across servers, so results interleave by per-source rank in member-priority order.
- Every merged item gains a display-only `sourceLabel`. `memberId` never enters item maps, `MediaRef`, or caches.

Labels come from `BloomProfile::memberLabel`: the member's `labelOverride`, otherwise `account · server`. The account leads, so two users on one server stay distinguishable.

## UI

- **Naming.** The UI calls Bloom profiles *spaces*. "Profile" already means a Silo or Jellyfin user profile, an mpv profile, and a Seerr quality profile. The code keeps `BloomProfile`, `WorkspaceController`, and the QML `Workspaces` context property. A space's members are *sources*, and saved connections are *saved accounts*. The default member is the *primary* source.
- **Mode follows membership.** The UI has no Merged/Single switch. Adding a second source makes a space merged (`addMember`). Removing a source that leaves one makes it single again (`removeMember`), and that remaining source is shown even if it was hidden. `setProfileMerged` stays in the API.
- **Summaries.** `Workspaces.profiles` gives each space a `sourceCount` and a display `detail`. A one-source space shows its source label, or the server type and host when the space is named after that source. A space with several sources shows the count, its shown sources, and how many are hidden. `SpaceAvatar.qml` draws a monogram tile, plus a second tile peeking out behind it when the space has several sources, so the difference does not rely on color.
- **Space picker** (`SpacePickerDialog.qml`): opened from the rail's account entry, which shows the active space's name and "Switch space". Spaces and the actions (Manage spaces, Add a server or account, Sign out) form one list in one row style, with a divider between them. Up/Down walk it, Enter selects, and Escape/Back closes. The current space has a check and "Current". Focus opens on the current space and returns to the rail entry on cancel. The login screen offers the same picker without sign-out and management ("Open a saved space"). While a connection is being added, it offers "Cancel and go back" instead. Escape and Back do the same.
- **Settings → Spaces** (`settings/SpaceSettings.qml`) has two levels in one row list. Each row is a single focus stop, and Enter does what its trailing cue shows. The overview lists spaces (avatar, name, summary, chevron), a New space row, and Saved accounts (Enter asks to remove the account and its saved sign-in). Enter on a space opens its detail page. The detail page has a back link and a header with the avatar, name, and a one-line explanation. Below that are an action row (Switch to space, Rename, Move up/down, Delete), the Sources list, and "Add a server or account". Enter on a source opens a `BloomMenu`: Hide from/Show in this space, Make primary, Rename, Move up/down, Remove from space (only offered when the space has more than one source). Up/Down move between rows, and Left/Right move within the action row. Left from a row's first control returns to the settings rail. On the detail page, Escape/Back returns to the overview with focus on that space. The page takes Escape through `Keys.onShortcutOverride`, so Main's Back shortcut does not leave Settings. Destructive actions confirm first. Focus returns to the same logical row with `Qt.callLater` after the model refreshes.
- **Home, Search, rail**: merged spaces read through `MergedCatalog`. Library tiles, Recently Added headers, and rail entries show the source. Continue Watching/Up Next cards and search results carry a `SourceBadge`. Per-library rows key on `connectionId + libraryId`, because two users on one server share library IDs. Saved rail library order and Shift+Enter reordering apply to single spaces only. Backdrops and the Library hero source come from the live session's connection.

## Migration and rollback

This feature adds no config or profile schema version. Config stays at v30 and `settings.bloom_profiles` at schema 1. Rolling back to a build without space switching keeps working. That build restores `settings.connections.active`, which holds the last live connection, and keeps the profile block as data. Credentials are untouched. Connections removed in this build stay removed after a rollback, and their credentials are already deleted.

## Tests

`MergedCatalogPolicyTest` covers merge ordering, failed-source isolation, no deduplication, same-server users, and labels. `WorkspaceRoutingTest` runs real `ConfigManager`, repository, `AuthenticationService`, `MergedCatalogService`, and `WorkspaceController` against fake servers that answer only to each connection's own token. It covers per-member credentials in merged reads, two users on one server, an unavailable member, dropping in-flight results on a profile switch, routing that keeps credentials, failed-route fallback, routes and switches that reach a profile or PIN prompt, the restoring flag across a skipped member, sign-out forgetting pooled tokens, joining a space after sign-in, connection removal isolation, pooled-member artwork, and a space turning single when it is left with one source. `NavigationRailTest` covers the account entry opening the picker.

`BloomProfileRepositoryTest` covers JSON round-trip, profile reordering, connection removal that keeps sibling memberships, multi-server plus two users on one physical server (shared `baseUrl`, distinct `connectionId`s), duplicate-connection dedupe, migration seeding (active-only, sole connection, empty), dangling repair, ordering/default repair, Single-mode multi-member retention, CRUD persistence, context epoch stale guards, and absence of credential/server identity fields in profile JSON.

See also: [provider architecture](provider-architecture.md), [canonical models](canonical-models.md), [config](config.md), [services](services.md).
