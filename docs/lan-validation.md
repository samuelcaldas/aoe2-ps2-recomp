# LAN infrastructure and validation gate

## Status — 2026-10-21

**Multiplayer is not functional.** The shared network transport (`net/`, with
its POSIX UDP backend in `platform/desktop`) carries a local advertisement
codec and an expiring session directory (`net/.../lan_discovery`). Discovery
is not connected to guest execution. There are no LAN runtime
hooks, lobby integration, or supported game-runner LAN options yet.

Network matching remains incomplete. Partial exact matches reported by the
matching session do not establish the full guest contract. In particular,
the session handshake, receive-ring consumer, exact message layout, socket
blocking behavior, and higher-layer delivery/lockstep guarantees remain open.
A table with eight slots does not prove an end-to-end player limit. Hardware
timer counts must not be treated as milliseconds without proving the timebase.

No generated game units are needed for the tests below. No two-instance game
validation has run. Boot failures remain separate from LAN failures.

## Host transport contract

Discovery uses the shared `INetworkTransport` (`net/include/aoe2/net/network_transport.hpp`),
the same interface the i.LINK socket model uses. This branch's own earlier UDP
transport was dropped in favor of it.

- `Endpoint.ipv4` and `port` use host byte order (loopback is `0x7f000001`);
  conversion happens at the socket boundary in the backend.
- Receiving is nonblocking: `TryReceive()` returns no datagram when empty. Sends
  are datagrams, not reliable messages; submission does not prove delivery,
  ordering or duplicate suppression.
- Tests exercise the directory over `InMemoryHub`. The POSIX UDP backend has its
  own tests (`udp_transport_tests`). Real LAN broadcast discovery is not tested.
- The POSIX backend is desktop-only. Switch compatibility has not been validated.

## Local discovery wire contract

This protocol belongs to the new host library. It is not a reconstruction of
the original transport or a promise of compatibility with another edition.
All multibyte integers use big-endian encoding.

| Offset | Length | Field |
| --- | --- | --- |
| 0 | 4 | ASCII magic `A2LN` |
| 4 | 1 | Version, currently 1 |
| 5 | 8 | Nonzero compatibility identifier |
| 13 | 8 | Nonzero session identifier |
| 21 | 1 | Advertised current player count |
| 22 | 1 | Nonzero advertised capacity |
| 23 | 1 | Name length in bytes |
| 24 | 1–64 | Nonempty UTF-8 session name, without NUL |

The current player count cannot exceed advertised capacity. A packet must
contain exactly its declared name length, without trailing bytes. Invalid
UTF-8, incompatible identifiers, unsupported versions, malformed packets, and
oversized packets are rejected before directory mutation. Identifiers and
advertisements are not authentication; discovery assumes an untrusted LAN.
Do not use the capacity field as proof of how many players the game supports.

The caller supplies the compatibility identifier, monotonic clock, positive
expiry duration, socket port, and per-poll packet budget. There is no default
LAN port. Port 23000 remains a proposal only. A runtime compatibility identifier
must not be chosen until the integration contract is established.

A directory holds at most 256 entries, keyed by session identifier plus source
endpoint. A valid repeated advertisement refreshes its entry. An entry expires
at the configured age; listing removes expired entries. A changed endpoint is
a distinct entry until the old one expires. This is an announcement cache,
not a replay-resistant session handshake: replayed valid advertisements can
refresh an entry. The directory does not authenticate freshness or remote
liveness. Discovery identity and replay handling must be evaluated before
runtime integration.

`Poll()` consumes at most its packet budget and returns the processed count
plus each rejected packet's source and `DiscoveryError`. It does not broadcast,
schedule advertisements, or join a session. The caller owns those operations.

## Lightweight verification

Run from the repository root in the isolated worktree:

```bash
python3 -m unittest discover -s tests
cmake -S . -B build/lan-unit -G Ninja
cmake --build build/lan-unit --target aoe2_lan_discovery_tests aoe2_net_tests -j 3
ctest --test-dir build/lan-unit -R "lan_discovery|net_tests" --output-on-failure
git diff --check
```

The discovery tests cover codec roundtrip/rejections, duplicate advertisements,
expiry with a fake clock, rejected-packet reporting, and a packet budget when
polling an in-memory transport. They do not
prove simulation determinism, guest ABI correctness, reliable delivery,
real broadcast discovery, or gameplay.

## Prerequisites for the real two-instance test

Tasks for session semantics and runtime integration remain blocked until all
necessary guest functions compare exactly, including relocations, and the
full lifecycle, identity, layout, player-limit, and delivery contracts have
been established. Raw game evidence stays ignored; tracked reports contain
semantics only.

Before execution, coordinate with `aoe-recomp` on one validated integration
commit, matching game inputs, explicit LAN configuration, two displays, and
nonconflicting endpoints. `aoe-recomp` owns the game build/run for this gate;
do not duplicate a full generated-unit build from the networking session.
The library tests above do not require an upstream runtime build.

## Two-instance test procedure — not executed

Once those prerequisites are met:

1. Record both instance configurations and the integration commit. Keep local
   logs and input-command traces under ignored `build/lan-validation/`.
2. Boot both instances and prove that each reaches the multiplayer lobby.
   If either stops before the lobby, record a boot blocker and mark LAN
   **not tested**, rather than treating a live process as success.
3. Create a named session in one instance. Discover it in the other instance
   and verify compatibility filtering and removal after advertisement expiry.
4. Join the session. Confirm peer identity and the verified capacity/admission
   policy on both sides. Record rejection behavior for incompatible peers.
5. Start the same match. Issue commands from both players and verify that both
   instances apply them on the corresponding simulation turns. Compare the
   agreed deterministic state evidence, not screenshots alone.
6. Exercise disconnect and shutdown. Check that the peer state, queues, and
   runtime-owned socket resources are released without partial hooks.
7. Have `aoe-recomp` report boot, discovery, joining, match progression, and
   synchronization separately. Add sanitized results and limitations here.

Only that real test can establish the LAN milestone. Socket tests and partial
matching cannot substitute for it. No Internet, PC/HD cross-play, or support
beyond two players is claimed.
