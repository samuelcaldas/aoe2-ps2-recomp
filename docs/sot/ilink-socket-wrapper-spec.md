# i.LINK socket wrapper specification (EE side)

Source of truth for the host replacement of the game's `sceILsock*` boundary
(`kcet.net.*` hooks, `net/`). Derived from the owned ELF (instruction structure) and
the owner's matched network sources; contains roles and service ids only, no guest
addresses or binary symbol names.

## Wrapper shape

Each wrapper fills a small request buffer, performs one SIF RPC call on its own bound
client, and returns **word 0 of the reply buffer** (or the RPC error if the call
fails). The RPC function number equals the service id. Eight clients are bound at init;
this binary uses these:

| Role | SID | Request buffer (words) | Result |
|---|---|---|---|
| `kcet.net.open` | 0x80000b88 | `[domain, type, protocol]` called as `(60, 2, 0)` | socket id |
| `kcet.net.bind` | 0x80000b8a | `[sock, len, address(16)]`; `len != 16` → `-2007` before any RPC | 0 or error |
| `kcet.net.connect` | 0x80000b8b | same; `len > 16` → `-2007` | 0 or error |
| `kcet.net.send` | 0x80000b8c | `[sock, len, data]`; `len >= 493` → `-2007` | bytes sent |
| `kcet.net.recv` | 0x80000b8e | `[sock, len]`; `len >= 493` → `-2007` | bytes received |
| `kcet.net.recvfrom` | 0x80000b8f | `[sock, len]`; `len >= 493` → `-2007`; `*addr_len` must be 16 → `-2007` | bytes received |
| `kcet.net.node_id` | 0x80000bc0 | none; copies the 8 reply bytes after word 0 to the caller | 0 or error |

There is no close or sendto wrapper in this binary (SIDs 0xb89 and 0xb8d are bound but never
called), so those roles do not exist.

## Caller conventions that fix the host semantics

- Datagrams are exactly 492 bytes. The receiver thread acts only on a return value of
  exactly 492, so "no packet" is any other value; `0` is used.
- Address record (16 bytes, little-endian fields): `len=16`, `family=60`, `port`,
  two node words, 4 tail bytes. Receive port is 0x3412 (field value); the broadcast
  node is all ones.
- The guest compares the sender node in a `recvfrom` record against the **byte-swapped**
  words returned by `node_id`. Records therefore carry `bswap32(eui word)`.
- Bind and connect retry five times and treat any non-zero result as failure; open
  results are stored without being checked.
- Errors: `-2007` is returned by the wrappers themselves (argument size). `-2000` is
  the value the guest pre-sets; its meaning as a service error is unverified, so the
  host uses it only as a generic failure.

## Host mapping (this port, not the original wire format)

Node word 0 is `0xAE20<<16 | instance`, node word 1 is the IPv4 address. UDP port is
`port + instance`. Datagrams carry a small host header with the sender node. Byte
fidelity with a real PS2 on the wire cannot be verified (no capture exists).
