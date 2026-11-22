# Vendored third-party headers

These files are byte-for-byte copies of tagged upstream releases. Do not edit them locally.
Updating either library requires a decision ticket because `AGENTS.md` limits third-party
libraries to these two.

| Library | Version | License | Upstream | File |
| --- | --- | --- | --- | --- |
| doctest | v2.5.3 | MIT | https://github.com/doctest/doctest | `doctest/doctest.h` |

Fetch command used (run once, never at build time):

    Invoke-WebRequest -Uri "https://raw.githubusercontent.com/doctest/doctest/v2.5.3/doctest/doctest.h" -OutFile "third_party/doctest/doctest.h"

toml++ v3.4.0 (MIT, https://github.com/marzer/tomlplusplus) will be vendored as
`toml++/toml.hpp` when config work begins in milestone 6.
