#include <doctest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "config_file.h"
#include "util/config_path.h"

namespace {

class EnvOverride {
public:
    EnvOverride(const wchar_t* name, const std::wstring& value) : name_(name) {
        const DWORD size = GetEnvironmentVariableW(name_, nullptr, 0);
        if (size > 0) {
            previous_.resize(size);
            GetEnvironmentVariableW(name_, previous_.data(), size);
            previous_.resize(size - 1);
        }
        SetEnvironmentVariableW(name_, value.c_str());
    }

    ~EnvOverride() {
        SetEnvironmentVariableW(name_, previous_.empty() ? nullptr : previous_.c_str());
    }

private:
    const wchar_t* name_;
    std::wstring previous_;
};

std::wstring testDirectory() {
    return (std::filesystem::temp_directory_path() / L"tapir-config-file-test").wstring();
}

void writeFile(const std::wstring& path, const std::string& content) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << content;
}

std::string readFile(const std::wstring& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    return std::string(std::istreambuf_iterator<char>(input),
                       std::istreambuf_iterator<char>());
}

void resetDirectory() {
    std::error_code error;
    std::filesystem::remove_all(testDirectory(), error);
    std::filesystem::create_directories(testDirectory(), error);
}

} // namespace

TEST_CASE("config paths follow the TAPIR_CONFIG_DIR override") {
    const EnvOverride override(L"TAPIR_CONFIG_DIR", testDirectory());
    CHECK(tapir::util::config::directory() == testDirectory());
    CHECK(tapir::util::config::configPath().find(L"config.toml") != std::wstring::npos);
    CHECK(tapir::util::config::logPath().find(L"tapir.log") != std::wstring::npos);
}

TEST_CASE("first run writes a commented default config and never overwrites it") {
    const EnvOverride override(L"TAPIR_CONFIG_DIR", testDirectory());
    resetDirectory();

    REQUIRE(tapir::config_file::ensureDefaults());
    const std::string initial = readFile(tapir::util::config::configPath());
    CHECK(initial.find("[general]") != std::string::npos);
    CHECK(initial.find("Ctrl+Space") != std::string::npos);
    CHECK(initial.find('#') != std::string::npos);

    writeFile(tapir::util::config::configPath(), "[general]\nhotkey = \"Ctrl+K\"\n");
    REQUIRE(tapir::config_file::ensureDefaults());
    CHECK(readFile(tapir::util::config::configPath()) ==
          "[general]\nhotkey = \"Ctrl+K\"\n");
}

TEST_CASE("malformed TOML keeps the last good settings") {
    const EnvOverride override(L"TAPIR_CONFIG_DIR", testDirectory());
    resetDirectory();
    writeFile(tapir::util::config::configPath(), "[general]\nhotkey = \"Ctrl+K\"\n");

    tapir::config_file::Watcher watcher;
    REQUIRE(watcher.start(tapir::defaultSettings()));
    CHECK(watcher.settings().general.hotkey == L"Ctrl+K");
    CHECK(watcher.refresh().settings.general.hotkey == L"Ctrl+K");

    writeFile(tapir::util::config::configPath(), "[general\nhotkey = \"Alt+F4\"\n");
    const auto result = watcher.refresh();
    CHECK_FALSE(result.valid);
    CHECK(result.settings.general.hotkey == L"Ctrl+K");
}

TEST_CASE("diagnostics log warnings without echoing values") {
    const EnvOverride override(L"TAPIR_CONFIG_DIR", testDirectory());
    resetDirectory();

    tapir::config_file::Watcher watcher;
    REQUIRE(watcher.start(tapir::defaultSettings()));
    writeFile(tapir::util::config::configPath(),
              "[history]\nmax_entries = \"SECRET-CLIPBOARD-TEXT\"\n");
    const auto result = watcher.refresh();
    REQUIRE_FALSE(result.diagnostics.empty());
    CHECK(result.settings.history.maxEntries == 500);

    const std::string log = readFile(tapir::util::config::logPath());
    CHECK(log.find("warning:") != std::string::npos);
    CHECK(log.find("SECRET-CLIPBOARD-TEXT") == std::string::npos);
}
