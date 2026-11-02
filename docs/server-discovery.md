# Server discovery on the local network

The login screen (signed out, or "Add a server" from the space picker) lists
the Jellyfin and Silo servers on the local network under **Found on your
network**, so a gamepad user can pick one instead of typing an address.
Choosing a server fills the address field and continues exactly as if the
address had been typed: sign-in options are checked, the server's protocol is
detected, and every sign-in rule stays the same. Manual entry is always there.

Everything found is a hint. An advertisement can come from any host, so a
server is listed only after its own public identity read at the found address
answers the advertised server id. The id is never a credential: it groups
addresses, it authorizes nothing (Silo's
[server identity](https://github.com/Silo-Server/silo-server/blob/a103c2c885f20ea1b11160c233d04994a0884014/docs/architecture/server-identity.md)).

## Components

`Bloom::ServerDiscovery` (`src/network/discovery/`) sits beside
`Bloom::Network`: it links only `Bloom::Providers` (models → configuration and
transport → providers → discovery) and needs no session.

| Piece | Role |
| --- | --- |
| `DnsMessage` | The DNS wire format (RFC 1035, 6762, 6763): queries and responses with name compression; decodes PTR, SRV, TXT, A and AAAA and skips other types. Bounded: a malformed packet (truncated, label or name too long, a compression pointer that does not point strictly backwards, a name that follows more than 32 pointers, more than 256 questions and records declared) is rejected whole. |
| `DnsSdResolver` | Assembles responses into resolved instances of one service type (PTR → SRV + TXT → A/AAAA), honors goodbyes (TTL 0) and cache-flush address sets (A and AAAA flushed separately, RFC 6762 §10.2), forgets the addresses of hosts no instance points at any more, drops loopback and link-local IPv6 addresses, asks follow-up questions for what is missing, and keeps at most 32 instances and 8 addresses per host. |
| `MdnsServerBrowser` | The multicast DNS querier (below). |
| `JellyfinServerBrowser` | Jellyfin's UDP broadcast. |
| `SiloLanDiscovery`, `JellyfinLanDiscovery` (`src/providers/…`) | The providers' advert formats and identity parsing; wire fields stay in the provider directories. |
| `ProviderServerValidator` | Confirms a candidate through the provider adapter: `ProviderRoute::ServerIdentity` and `IProviderAdapter::mapServerIdentity`, then the optional `ProviderRoute::ServerBranding` and `mapServerBrandingName`. |
| `ServerDiscoveryService` | QML `ServerDiscovery`: lifetime, timing, dedupe and the `servers` list. |

No `ProviderCapability` bit is involved; discovery runs before any server is
chosen.

## Lifetime

`LoginScreen` sets `ServerDiscovery.active` while its address form is on
screen (the screen is visible and on the credentials step, not the code,
profile or PIN steps) and clears it when the screen hides or is destroyed.
Each activation is one bounded search:

- The browsers open their sockets and ask at 0, 1, 3, 7 and 15 seconds
  (RFC 6762 §5.2 spacing), then close their sockets 20 seconds after the
  start. "Search again" (shown when nothing was found) starts another search.
- Deactivating closes the sockets at once, cancels running identity reads and
  clears the list; a late answer from an earlier search is ignored.
- Nothing blocks the UI thread: sockets are read on `readyRead` (at most 16
  mDNS or 64 Jellyfin datagrams per event, the rest on the next pass), and
  identity reads go through `HttpTransport`.
- In `--test-mode` the service is inert: no sockets, nothing listed, and the
  section is hidden (`available` is false). It is also hidden when neither
  browser can open a socket.

## Silo: DNS-SD over multicast DNS

Silo advertises each API process as `_silo._tcp.local` with TXT `v=1` and
`id=<server_id>` and an SRV port that speaks plain HTTP
([Silo server discovery](https://github.com/Silo-Server/silo-server/blob/a103c2c885f20ea1b11160c233d04994a0884014/docs/architecture/server-discovery.md)).
Bloom asks for PTR `_silo._tcp.local` and reads the SRV, TXT and address
records Silo adds to its answer. An instance whose TXT `v` is not `1` or that
carries no `id` is skipped; unknown TXT keys are ignored. Candidates are
`http://<address>:<port>` for each address, IPv4 first, IPv6 in brackets.
Link-local IPv6 never becomes a URL: Qt's networking cannot carry the zone
reliably in a base URL, and Silo never advertises one.

### Why a querier of our own, sharing UDP 5353

Silo's responder (`internal/landiscovery/responder.go`) drops any packet whose
source port is not 5353 and answers only by multicast to the link's group,
never to the sender. A legacy-unicast querier on an ephemeral port therefore
gets no reply. Bloom's querier binds UDP 5353 itself with
`QAbstractSocket::ShareAddress | ReuseAddressHint` (`SO_REUSEADDR`), joins
224.0.0.251 and ff02::fb on every up, multicast-capable, non-loopback,
non-point-to-point interface, sends its queries from 5353 to the group, and
reads Silo's multicast answers. Silo's responder shares the port with the
system's mDNS daemon in the same way.

- **Linux:** avahi and systemd-resolved bind 5353 with `SO_REUSEADDR`, so the
  shared bind succeeds next to them and every socket on the port receives the
  group's datagrams. Checked on a Linux host with the system mDNS daemon
  running: a `SO_REUSEADDR` socket bound beside it on 5353 received the
  multicast answers to its query. While Bloom's socket is open (at most 20
  seconds) a unicast datagram to 5353 may reach Bloom instead of the daemon
  (RFC 6762 §15.1); queries ask for multicast answers, so this is rare and
  short.
- **Windows:** the DNS Client service holds 5353 without exclusive use, and
  `SO_REUSEADDR` sockets all receive multicast, the arrangement Chromium and
  other mDNS libraries rely on. Windows Firewall may ask to allow Bloom on
  private networks the first time.
- Only packets from this link are read: an IP hop limit of 255 (RFC 6762
  §11, where Qt reports it), an IPv6 link-local sender, or a sender inside one
  of the browsing interfaces' subnets. Silo's responder sends with Go's
  default multicast TTL of 1, so the subnet test is what admits it; a
  responder on another subnet of the same link is not read.
- If 5353 cannot be shared, the mDNS browser reports itself unavailable,
  logs it under `bloom.discovery`, and Silo servers are not listed; Jellyfin
  discovery and manual entry are unaffected. There is no unicast fallback,
  because Silo would not answer it.

Platform browse APIs (avahi over D-Bus, `DnsServiceBrowse`) were not needed:
the shared socket works on both platforms without a daemon dependency, and
the same code runs everywhere. If manual validation finds a host where 5353
cannot be shared, a platform browser behind `LanServerBrowser` is the
fallback to add.

## Jellyfin: UDP broadcast

Bloom sends `Who is JellyfinServer?` from an ephemeral port to UDP 7359 at
255.255.255.255 and at each IPv4 interface's directed broadcast address.
Jellyfin (`AutoDiscoveryHost`, on unless *Enable auto discovery* is off)
answers the sender with JSON: `Address`, `Id`, `Name`, `EndpointAddress`.
Replies must come from port 7359 and be at most 4 KiB. Candidates, in order:

1. `Address`: a full `http`/`https` URL keeps its scheme, port and base path;
   a bare `host[:port]` (as a published server URL can be) becomes
   `http://host:port`, port 8096 when absent.
2. `EndpointAddress`, read the same way when present.
3. The address the reply came from, under the advertised base path, on the
   advertised port when `Address` is plain HTTP, else on 8096 (an HTTPS port
   does not serve HTTP). A published `Address` can be unreachable from the
   LAN while the sender address is not.

## Validation and dedupe

Each candidate gets the provider's public identity read: Silo
`GET /api/v2/system/identity` (`getServerIdentity`), Jellyfin
`GET /System/Info/Public` (`GetPublicSystemInfo`). The answer must name the
advertised id; otherwise the candidate is dropped. A Silo entry is then
labelled from `GET /api/v2/theme/branding` (`getBranding`) `server_name`,
falling back to the advertised instance name when that read fails; Jellyfin's
label is `ServerName`. The reads carry no `Authorization` header (the
validator removes the MediaBrowser client header Jellyfin's request factory
adds), no token, profile or device headers, follow no redirect (the candidate
itself must answer), make one attempt with a 3-second deadline and buffer at
most 64 KiB; a larger body is never read whole, so the check fails.

Entries are keyed by provider, server id and the advert's origin: the
DNS-SD host name (SRV target) for Silo, the reply's sender address for
Jellyfin. Several addresses from one origin (IPv4 and IPv6, a published
`Address` and the sender fallback) are validated in parallel, at most four
reads at a time; the first address that confirms is listed and that origin's
other candidates are dropped. The same id confirmed from another origin is
listed as its own entry with its own address, so a host that copies a
server's id cannot take over that server's card by answering first; the card
shows the address to tell them apart. An address is checked once per server
id and search, whichever origin advertised it, and a failed one is not tried
again. The list holds at most 16 entries and 64 candidate addresses per
search, in the order they confirmed, and one sending address can queue at
most 8 candidates, so a single host cannot use up the whole budget.

An entry is marked `saved` (the card reads "Saved") when a saved connection
has its provider, server id and host. The id alone is the advertiser's claim,
so the same id at another host is not marked.

## UI

`LoginScreen` shows **Found on your network** above the address field: a
horizontal row of cards (server name, provider, address and "Saved" for a
saved server, all plain text since names are untrusted) with the shared
`FocusRing`, or a compact `StateMessage` while searching ("Looking for servers
on your network…") or when nothing was found ("No servers found. Enter the
address below." with **Search again**). Up from the address field reaches the
row (or Search again); Left/Right move along it; Enter or A fills the address
and moves on to the username, or to the code sign-in when the server turns out
to offer only that. Down returns to the address field, and the form's bottom
wraps to the row. The row is a QML `ListModel` that the screen appends to, so
the focused card keeps its place while servers arrive; if the row empties
under focus, focus returns to the address field with `Qt.callLater`.

## Not covered

- **Multi-address failover.** A saved connection keeps the one address that
  was chosen. Silo's `GET /api/v2/system/connections` (`getServerConnections`)
  and the pinned local route are a follow-up.
- Overlay networks (Tailscale short names) and servers outside the LAN.
- Silo's Jellyfin compatibility listener does not answer the Jellyfin probe;
  native Silo is found through DNS-SD.

## Verification

`ServerDiscoveryTest` covers DNS encoding and decoding (PTR, SRV, TXT, A,
AAAA, compression, a hand-written response as Silo packs it, malformed and
looping packets), DNS-SD resolution, Silo advert rules, Jellyfin reply parsing
including a real reply whose `Address` is unreachable and the HTTPS base-path
fallback, the pointer-hop and entry caps, per-family cache flush and host
cache pruning, the on-link sender test, the validator against a local HTTP
stub (identity match, branding, redirects and errors refused, no credential
or Authorization headers, the 64 KiB body limit, the 3-second deadline), and
the service with fake browsers and a fake validator: lifetime, bounded
re-queries, dedupe per origin, the per-sender cap, saved marking, dropped
candidates and bounded reads.
Live LAN behavior is manual validation ([ledger](manual-validation.md)).
