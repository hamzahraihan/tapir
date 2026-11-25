#include <doctest.h>

#include <string>
#include <string_view>
#include <vector>

#include "config_writer.h"

using tapir::config_writer::Value;
using tapir::config_writer::patch;

namespace {

std::string apply(std::string_view original, std::vector<Value> values) {
    return patch(original, values);
}

} // namespace

TEST_CASE("replaces a known value in its table") {
    const std::string input = R"toml([general]
version = 1
hotkey = "Ctrl+Space"
animations = true
)toml";
    const std::string expected = R"toml([general]
version = 1
hotkey = "Ctrl+Space"
animations = false
)toml";

    CHECK(apply(input, {{"general.animations", "false"}}) == expected);
}

TEST_CASE("preserves comments, indentation, and spacing around the value") {
    const std::string input = R"toml(# tapir configuration
[general]
    animations = true        # keep this comment
	hotkey = "Ctrl+Space"   # Ctrl joined with +
)toml";
    const std::string expected = R"toml(# tapir configuration
[general]
    animations = false        # keep this comment
	hotkey = "Alt+Space"   # Ctrl joined with +
)toml";

    CHECK(apply(input,
                {{"general.animations", "false"},
                 {"general.hotkey", "\"Alt+Space\""}}) == expected);
}

TEST_CASE("a hash inside a string is not a trailing comment") {
    const std::string input = R"toml([theme]
font = "A#B"   # display font
)toml";
    const std::string expected = R"toml([theme]
font = "C#D"   # display font
)toml";

    CHECK(apply(input, {{"theme.font", "\"C#D\""}}) == expected);
}

TEST_CASE("keeps a comment that directly follows the value") {
    const std::string input = R"toml([general]
animations = true# keep
)toml";
    const std::string expected = R"toml([general]
animations = false # keep
)toml";

    CHECK(apply(input, {{"general.animations", "false"}}) == expected);
}

TEST_CASE("preserves unknown keys and their order") {
    const std::string input = R"toml([general]
version = 1
mystery = 42   # unknown to tapir
hotkey = "Ctrl+Space"
)toml";
    const std::string expected = R"toml([general]
version = 2
mystery = 42   # unknown to tapir
hotkey = "Ctrl+Space"
)toml";

    CHECK(apply(input, {{"general.version", "2"}}) == expected);
}

TEST_CASE("adds a missing key at the end of its table") {
    const std::string input = R"toml([general]
version = 1
hotkey = "Ctrl+Space"

[window]
width = 680
)toml";
    const std::string expected = R"toml([general]
version = 1
hotkey = "Ctrl+Space"
animations = true

[window]
width = 680
)toml";

    CHECK(apply(input, {{"general.animations", "true"}}) == expected);
}

TEST_CASE("creates a missing table at the end of the file") {
    const std::string input = R"toml([general]
version = 1
)toml";
    const std::string expected = R"toml([general]
version = 1

[theme]
accent = "#cba6f7"
)toml";

    CHECK(apply(input, {{"theme.accent", "\"#cba6f7\""}}) == expected);
}

TEST_CASE("creates a table in an empty file") {
    const std::string expected = R"toml([general]
version = 1
)toml";

    CHECK(apply("", {{"general.version", "1"}}) == expected);
}

TEST_CASE("applies multiple values in one call") {
    const std::string input = R"toml([general]
version = 1
hotkey = "Ctrl+Space"

[theme]
accent = "#cba6f7"

[history]
max_entries = 500
)toml";
    const std::string expected = R"toml([general]
version = 1
hotkey = "Alt+Space"
animations = false

[theme]
accent = "#ff0000"

[history]
max_entries = 500

[privacy]
encrypt_history = false
)toml";

    CHECK(apply(input,
                {{"general.animations", "false"},
                 {"general.hotkey", "\"Alt+Space\""},
                 {"theme.accent", "\"#ff0000\""},
                 {"privacy.encrypt_history", "false"}}) == expected);
}

TEST_CASE("an empty value list returns the original text") {
    const std::string input = "[general]\nversion = 1\n";
    CHECK(apply(input, {}) == input);
}

TEST_CASE("preserves CRLF line endings") {
    const std::string input =
        "[general]\r\n"
        "version = 1\r\n"
        "animations = true\r\n"
        "\r\n"
        "[theme]\r\n"
        "accent = \"#cba6f7\"\r\n";
    const std::string expected =
        "[general]\r\n"
        "version = 1\r\n"
        "animations = false\r\n"
        "hotkey = \"Alt+Space\"\r\n"
        "\r\n"
        "[theme]\r\n"
        "accent = \"#ffffff\"\r\n";

    CHECK(apply(input,
                {{"general.animations", "false"},
                 {"general.hotkey", "\"Alt+Space\""},
                 {"theme.accent", "\"#ffffff\""}}) == expected);
}

TEST_CASE("writes array, string, boolean, and number literals") {
    const std::string input = R"toml([window]
width = 680
compact = true

[theme]
font = "Segoe UI Variable"

[index]
sources = ["appsfolder", "startmenu"]
)toml";
    const std::string expected = R"toml([window]
width = 900
compact = false

[theme]
font = "Consolas"

[index]
sources = ["startmenu"]
)toml";

    CHECK(apply(input,
                {{"window.width", "900"},
                 {"window.compact", "false"},
                 {"theme.font", "\"Consolas\""},
                 {"index.sources", "[\"startmenu\"]"}}) == expected);
}

TEST_CASE("does not treat comment lines as keys") {
    const std::string input = R"toml(# animations = false
[general]
animations = true
)toml";
    const std::string expected = R"toml(# animations = false
[general]
animations = false
)toml";

    CHECK(apply(input, {{"general.animations", "false"}}) == expected);
}

TEST_CASE("only touches the matching table section") {
    const std::string input = R"toml([general]
animations = true

[window]
animations = false
)toml";
    const std::string expected = R"toml([general]
animations = false

[window]
animations = false
)toml";

    CHECK(apply(input, {{"general.animations", "false"}}) == expected);
}
