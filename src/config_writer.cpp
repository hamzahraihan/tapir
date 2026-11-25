#include "config_writer.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace tapir::config_writer {
namespace {

struct Line {
    std::string content;
    std::string eol; // "\n", "\r\n", or "" for a final line with no newline
};

bool isSpace(char c) {
    return c == ' ' || c == '\t';
}

std::string_view trim(std::string_view text) {
    std::size_t begin = 0;
    while (begin < text.size() && isSpace(text[begin])) {
        ++begin;
    }
    std::size_t end = text.size();
    while (end > begin && isSpace(text[end - 1])) {
        --end;
    }
    return text.substr(begin, end - begin);
}

std::vector<Line> splitLines(std::string_view text) {
    std::vector<Line> lines;
    std::size_t start = 0;
    while (start < text.size()) {
        const std::size_t newline = text.find('\n', start);
        if (newline == std::string_view::npos) {
            lines.push_back(Line{std::string(text.substr(start)), std::string()});
            break;
        }
        std::size_t end = newline;
        if (end > start && text[end - 1] == '\r') {
            --end;
        }
        lines.push_back(Line{std::string(text.substr(start, end - start)),
                             end == newline ? std::string("\n") : std::string("\r\n")});
        start = newline + 1;
    }
    return lines;
}

std::string joinLines(const std::vector<Line>& lines) {
    std::size_t total = 0;
    for (const Line& line : lines) {
        total += line.content.size() + line.eol.size();
    }
    std::string out;
    out.reserve(total);
    for (const Line& line : lines) {
        out += line.content;
        out += line.eol;
    }
    return out;
}

std::string defaultEol(const std::vector<Line>& lines) {
    for (const Line& line : lines) {
        if (!line.eol.empty()) {
            return line.eol;
        }
    }
    return "\n";
}

// True for any "[...]" line, including forms this writer does not match.
bool looksLikeHeader(std::string_view content) {
    const std::string_view text = trim(content);
    return text.size() >= 2 && text.front() == '[' && text.find(']') != std::string_view::npos;
}

// Fills `name` with the trimmed table name when the line is a simple [table] header.
bool parseHeader(std::string_view content, std::string_view& name) {
    const std::string_view text = trim(content);
    if (text.size() < 2 || text.front() != '[') {
        return false;
    }
    const std::size_t close = text.find(']');
    if (close == std::string_view::npos) {
        return false;
    }
    const std::string_view rest = trim(text.substr(close + 1));
    if (!rest.empty() && rest.front() != '#') {
        return false;
    }
    const std::string_view inner = trim(text.substr(1, close - 1));
    if (inner.empty() || inner.front() == '[') {
        return false; // [[array-of-tables]] is out of scope
    }
    name = inner;
    return true;
}

struct Specials {
    std::size_t comment = std::string_view::npos;
    std::size_t equals = std::string_view::npos;
};

Specials findSpecials(std::string_view text, std::size_t from = 0) {
    Specials found;
    bool inDouble = false;
    bool inSingle = false;
    for (std::size_t i = from; i < text.size(); ++i) {
        const char c = text[i];
        if (inDouble) {
            if (c == '\\') {
                ++i;
            } else if (c == '"') {
                inDouble = false;
            }
        } else if (inSingle) {
            if (c == '\'') {
                inSingle = false;
            }
        } else if (c == '"') {
            inDouble = true;
        } else if (c == '\'') {
            inSingle = true;
        } else if (c == '#') {
            found.comment = i;
            return found;
        } else if (c == '=' && found.equals == std::string_view::npos) {
            found.equals = i;
        }
    }
    return found;
}

bool matchKey(std::string_view content, std::string_view keyName, std::size_t& equals) {
    const std::string_view text = trim(content);
    if (text.empty() || text.front() == '#' || text.front() == '[') {
        return false;
    }
    const std::size_t pos = findSpecials(content).equals;
    if (pos == std::string_view::npos) {
        return false;
    }
    if (trim(content.substr(0, pos)) != keyName) {
        return false;
    }
    equals = pos;
    return true;
}

// Replaces only the value span, keeping indentation, spacing, and any trailing comment.
void replaceValue(Line& line, std::size_t equals, std::string_view text) {
    const std::string_view content = line.content;
    const std::size_t marker = findSpecials(content, equals + 1).comment;
    const std::size_t comment =
        marker == std::string_view::npos ? content.size() : marker;

    std::size_t valueBegin = equals + 1;
    while (valueBegin < comment && isSpace(content[valueBegin])) {
        ++valueBegin;
    }
    std::size_t valueEnd = comment;
    while (valueEnd > valueBegin && isSpace(content[valueEnd - 1])) {
        --valueEnd;
    }

    std::string out;
    out.reserve(content.size() + text.size());
    out.append(content.substr(0, valueBegin));
    if (valueBegin == comment && valueBegin == equals + 1) {
        out.push_back(' '); // "key =" with no separating space
    }
    out.append(text);
    const std::string_view rest = content.substr(valueEnd);
    if (!rest.empty() && rest.front() == '#' && !out.empty() && !isSpace(out.back())) {
        out.push_back(' '); // "value# comment" would glue the value to the comment
    }
    out.append(rest);
    line.content = std::move(out);
}

std::string appendTable(std::vector<Line>& lines, std::string_view section, std::string_view name,
                        std::string_view text) {
    const std::string eol = defaultEol(lines);
    if (!lines.empty()) {
        Line& last = lines.back();
        if (last.eol.empty()) {
            last.eol = eol;
        }
        if (!trim(last.content).empty()) {
            lines.push_back(Line{std::string(), eol}); // blank separator before the new table
        }
    }
    lines.push_back(Line{"[" + std::string(section) + "]", eol});
    lines.push_back(Line{std::string(name) + " = " + std::string(text), eol});
    return joinLines(lines);
}

std::string patchOne(std::string_view original, const Value& value) {
    const std::size_t dot = value.key.rfind('.');
    const std::string_view section =
        dot == std::string::npos ? std::string_view() : std::string_view(value.key).substr(0, dot);
    const std::string_view name =
        dot == std::string::npos ? std::string_view(value.key) : std::string_view(value.key).substr(dot + 1);
    if (name.empty()) {
        return std::string(original);
    }

    std::vector<Line> lines = splitLines(original);

    std::string_view currentSection;
    bool inKnownSection = true; // the root before the first header
    for (Line& line : lines) {
        std::string_view header;
        if (parseHeader(line.content, header)) {
            currentSection = header;
            inKnownSection = true;
            continue;
        }
        if (looksLikeHeader(line.content)) {
            inKnownSection = false;
            continue;
        }
        if (!inKnownSection || currentSection != section) {
            continue;
        }
        std::size_t equals = 0;
        if (matchKey(line.content, name, equals)) {
            replaceValue(line, equals, value.text);
            return joinLines(lines);
        }
    }

    std::size_t headerIndex = 0;
    bool found = false;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        std::string_view header;
        if (parseHeader(lines[i].content, header) && header == section) {
            headerIndex = i;
            found = true;
            break;
        }
    }
    if (!found && !section.empty()) {
        return appendTable(lines, section, name, value.text);
    }

    const std::size_t lowerBound = found ? headerIndex + 1 : 0;
    std::size_t end = lines.size();
    for (std::size_t i = lowerBound; i < lines.size(); ++i) {
        if (looksLikeHeader(lines[i].content)) {
            end = i;
            break;
        }
    }
    std::size_t insertion = end;
    while (insertion > lowerBound && trim(lines[insertion - 1].content).empty()) {
        --insertion;
    }

    lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(insertion),
                 Line{std::string(name) + " = " + value.text, defaultEol(lines)});
    return joinLines(lines);
}

} // namespace

std::string patch(std::string_view original, std::span<const Value> values) {
    std::string result(original);
    for (const Value& value : values) {
        result = patchOne(result, value);
    }
    return result;
}

} // namespace tapir::config_writer
