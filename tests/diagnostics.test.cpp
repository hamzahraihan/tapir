#include <doctest.h>

#include <string>
#include <string_view>

#include "diagnostics.h"

using tapir::diagnostics::capOldestHalf;

TEST_CASE("log cap leaves a small log untouched") {
    const std::string log = "config.toml: unknown key\n";
    CHECK(capOldestHalf(log, 64 * 1024) == log);
}

TEST_CASE("log cap keeps the newest half on a line boundary") {
    std::string log;
    for (int i = 0; i < 12000; ++i) {
        log += "line " + std::to_string(i) + "\n";
    }

    const std::string capped = capOldestHalf(log, 64 * 1024);
    CHECK(capped.size() <= 32 * 1024);
    CHECK(capped.find("line 0\n") == std::string::npos);
    CHECK(capped.find("line 11999\n") != std::string::npos);
    CHECK(capped.find('\n') != std::string::npos);
    CHECK(capped.substr(0, capped.find('\n')).rfind("line ", 0) == 0);
}

TEST_CASE("log cap removes older text from the file") {
    std::string log = "SECRET-CLIPBOARD-CONTENT\n";
    log += std::string(70 * 1024, 'x');
    log += "\nlast line\n";

    const std::string capped = capOldestHalf(log, 64 * 1024);
    CHECK(capped.find("SECRET-CLIPBOARD-CONTENT") == std::string::npos);
    CHECK(capped.find("last line") != std::string::npos);
}
