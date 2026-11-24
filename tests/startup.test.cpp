#include <doctest.h>

#include <string>
#include <string_view>

#include "util/startup.h"

TEST_CASE("startup command line quotes a path with spaces") {
    CHECK(tapir::util::startup::commandLine(L"C:\\Program Files\\tapir\\tapir.exe") ==
          L"\"C:\\Program Files\\tapir\\tapir.exe\"");
}

TEST_CASE("startup command line quotes a path without spaces") {
    CHECK(tapir::util::startup::commandLine(L"C:\\tapir\\tapir.exe") ==
          L"\"C:\\tapir\\tapir.exe\"");
}

TEST_CASE("startup command line of an empty path is empty") {
    CHECK(tapir::util::startup::commandLine(L"").empty());
    CHECK(tapir::util::startup::commandLine(std::wstring_view{}).empty());
}

TEST_CASE("startup command line adds no arguments or escaping") {
    // Windows file names cannot contain a quote, so a literal quote is wrapped
    // verbatim rather than escaped.
    CHECK(tapir::util::startup::commandLine(L"C:\\tapir\\ti\"pir.exe") ==
          L"\"C:\\tapir\\ti\"pir.exe\"");
}
