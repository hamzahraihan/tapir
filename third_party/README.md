# Vendored third-party headers

These files are byte-for-byte copies of tagged upstream releases. Do not edit them locally.
Updating either library requires a decision ticket because `AGENTS.md` limits third-party
libraries to these two.

| Library | Version | License | Upstream | File |
| --- | --- | --- | --- | --- |
| doctest | v2.5.3 | MIT | https://github.com/doctest/doctest | `doctest/doctest.h` |
| toml++ | v3.4.0 | MIT | https://github.com/marzer/tomlplusplus | `toml/toml.hpp` |

Fetch commands used (run once, never at build time):

    Invoke-WebRequest -Uri "https://raw.githubusercontent.com/doctest/doctest/v2.5.3/doctest/doctest.h" -OutFile "third_party/doctest/doctest.h"
    Invoke-WebRequest -Uri "https://raw.githubusercontent.com/marzer/tomlplusplus/v3.4.0/toml.hpp" -OutFile "third_party/toml/toml.hpp"

`toml.hpp` SHA-256 at vendoring time: `6B5172AD4DD6519AEC67B919181FA7A38A2234131E5B2AFA232DFE444819783E`.
