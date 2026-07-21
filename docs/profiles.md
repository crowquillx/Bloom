# Bloom profiles

Bloom profiles are Bloom-owned workspaces for switching and (later) merging independent server connections. They are distinct from provider household profiles (for example Silo or Jellyfin user profiles).

This page documents the schema/repository slice (issue #91). Merged catalog reads, picker UI, and service fan-out are not implemented yet.

## Terminology

| Term | Meaning |
| --- | --- |
| **Connection** | One `ServerConnection` identity: server + account + provider profile + credential reference. |
| **Provider profile** | Server-owned user/household profile selected within a connection. |
| **Bloom profile** | User-named, ordered set of independent connection memberships. |
| **Membership** | One connection attached to a Bloom profile (`memberId` + `connectionId`). |
| **Single mode** | Exactly one membership; preserves today’s single-connection browsing behavior. |
| **Merged mode** | Multiple memberships; aggregation comes in a later slice. |

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
- `upsertProfile`, `removeProfile`, `setActiveProfile` (`upsertProfile` takes a reference and writes back assigned IDs/timestamps)
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

This layer does **not** secretly change `ConfigManager`’s active connection. Context is foundation-only until later service-routing slices.

## Types

- `BloomProfileMode { Single, Merged }`
- `BloomProfileMember` — `memberId`, `connectionId`, `enabled`, `priority`, optional `labelOverride`
- `BloomProfile` — stable UUID `id`, `name`, `mode`, ordered `members`, `defaultMemberId`, timestamps
- `BloomProfileRequestContext` — profile/member/connection + generation

Snake_case JSON; C++ API uses camelCase fields.

## Out of scope (later slices)

- Bloom profile picker / settings QML
- Merged Home/Library/Search coordinators
- Binding services to explicit request context instead of the global active connection
- Cross-server metadata deduplication

## Tests

`BloomProfileRepositoryTest` covers JSON round-trip, multi-server plus two users on one physical server (shared `baseUrl`, distinct `connectionId`s), duplicate-connection dedupe, migration seeding (active-only, sole connection, empty), dangling repair, ordering/default repair, Single-mode multi-member retention, CRUD persistence, context epoch stale guards, and absence of credential/server identity fields in profile JSON.

See also: [provider architecture](provider-architecture.md), [canonical models](canonical-models.md), [config](config.md), [services](services.md).
