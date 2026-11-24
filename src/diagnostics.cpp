#include "diagnostics.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>

namespace tapir::diagnostics {

std::string capOldestHalf(std::string_view content, std::size_t limitBytes) {
    if (content.size() <= limitBytes) return std::string(content);
    const std::size_t keep = limitBytes / 2;
    if (content.size() <= keep) return std::string(content);
    std::size_t start = content.size() - keep;
    const auto newline = content.find('\n', start);
    if (newline != std::string_view::npos) start = newline + 1;
    return std::string(content.substr(start));
}

void append(const std::wstring& logPath, std::string_view line) {
    std::string existing;
    {
        std::ifstream input(logPath, std::ios::binary);
        if (input) {
            existing.assign(std::istreambuf_iterator<char>(input),
                            std::istreambuf_iterator<char>());
        }
    }

    if (existing.size() + line.size() > kLogMaxBytes) {
        const std::string capped = capOldestHalf(existing, kLogMaxBytes);
        const std::wstring temporary = logPath + L".tmp";
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            if (!output) return;
            output.write(capped.data(), static_cast<std::streamsize>(capped.size()));
            output.write(line.data(), static_cast<std::streamsize>(line.size()));
            if (!output) return;
        }
        std::error_code error;
        std::filesystem::remove(logPath, error);
        std::filesystem::rename(temporary, logPath, error);
        return;
    }

    std::ofstream output(logPath, std::ios::binary | std::ios::app);
    if (!output) return;
    output.write(line.data(), static_cast<std::streamsize>(line.size()));
}

} // namespace tapir::diagnostics
