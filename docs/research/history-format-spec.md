# Settled on-disk clipboard history format (`history.dat`)

Source of truth: [issue #10 resolution comment](https://github.com/hamzahraihan/tapir/issues/10#issuecomment-7076456271) by hamzahraihan, 2026-11-22T08:20:16Z. Corroborated by [issue #16 "History storage" decision](https://github.com/hamzahraihan/tapir/issues/16) and [issue #1 wayfinder map](https://github.com/hamzahraihan/tapir/issues/1). No ADR or GLOSSARY files exist in the repo.

## Verbatim resolution (issue #10)

> Resolved by live grilling.
>
> **File shape:** an 8-byte plaintext prefix — ASCII `TPRH`, one version byte, one flag byte (bit 0 = payload encrypted), two reserved zero bytes — followed by the payload. The payload is a little-endian `uint32` entry count plus records of `uint32` UTF-16 code-unit length, `uint64` FILETIME, one pinned byte, and the UTF-16 bytes with no NUL terminator. Version 1 is the only accepted version.
>
> **Encryption:** per-user DPAPI (`CryptProtectData` without `CRYPTPROTECT_LOCAL_MACHINE`) over the payload only; the prefix stays readable. `encrypt_history = true` by default; when the setting changes, the next write adopts it and rewrites prefix and payload, with no explicit migration step.
>
> **List model:** one recency-ordered list. Each record stores text, pinned flag and timestamp only — no source process, hash or format metadata. A repeated copy moves the existing record to the top, refreshes its FILETIME and preserves its pinned flag. Pinned entries are exempt from caps and clearing.
>
> **Caps:** enforced on load using the current config — drop non-pinned entries beyond `max_entries` (500) and truncate or skip text beyond `max_entry_bytes` (1,048,576). Cleanup is persisted on the next debounced save.
>
> **Writes:** never in place. Write `history.dat.tmp` in the config directory, flush and close, then `MoveFileExW` with `MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH`. Debounce 500 ms after the last change, plus an immediate flush on clean exit. On failure, keep the in-memory history, remove the temp file, log one line in `tapir.log`, and retry on the next change; never crash or drop entries.
>
> **Corruption:** bad magic, failed decryption, unknown version or truncated payload renames the file to `history.dat.bak` (replacing an older backup), starts empty history and logs one line. No dialog; clipboard contents are never logged.

## Derived byte layout (from the quote above)

Prefix (8 bytes, plaintext): `TPRH` (4 ASCII) | version (1 byte, only 1 accepted) | flags (1 byte, bit 0 = payload encrypted) | reserved (2 bytes, zero).

Payload (little-endian; DPAPI-protected when flag bit 0 set): `uint32` entry count, then per record: `uint32` UTF-16 code-unit length | `uint64` FILETIME | 1 pinned byte | UTF-16 text bytes (no NUL terminator).

Filenames: `history.dat` (main), `history.dat.tmp` (write temp), `history.dat.bak` (single corruption backup), `tapir.log` (reason-only diagnostics).

## Ambiguities the spec does NOT resolve explicitly

- Endianness of the UTF-16 text bytes is not stated verbatim (only "UTF-16 bytes" inside a "little-endian" payload). Do not assume UTF-16LE without a decision.
- Bits 1-7 of the flag byte are not defined; only bit 0 is specified.
- Exact numeric value of the version byte is "1" but no hex/decimal representation is given beyond "version 1".

