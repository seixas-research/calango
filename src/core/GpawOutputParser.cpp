#include "core/GpawOutputParser.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace calango::core {

namespace {

/// Trim ASCII whitespace (including a trailing '\r' from a CRLF-written
/// file) from both ends.
std::string trimmed(const std::string& s)
{
    std::size_t begin = 0;
    while (begin < s.size()
           && std::isspace(static_cast<unsigned char>(s[begin])))
        ++begin;
    std::size_t end = s.size();
    while (end > begin
           && std::isspace(static_cast<unsigned char>(s[end - 1])))
        --end;
    return s.substr(begin, end - begin);
}

} // namespace

std::optional<int> parseGpawWorldSize(const std::string& gpawOutText)
{
    // The header is always the first ~20 lines; capped here rather than
    // scanning the whole (routinely tens of MB, for a long relaxation)
    // file, and capped generously enough to survive a header GPAW grows a
    // few lines in a future version.
    constexpr int kMaxHeaderLines = 60;
    std::istringstream stream(gpawOutText);
    std::string line;
    int lineCount = 0;
    while (lineCount < kMaxHeaderLines && std::getline(stream, line)) {
        ++lineCount;
        const std::string trimmedLine = trimmed(line);
        const std::string prefix = "cores:";
        if (trimmedLine.rfind(prefix, 0) != 0)
            continue; // does not start with "cores:"
        const std::string rest = trimmed(trimmedLine.substr(prefix.size()));
        if (rest.empty()
            || !std::all_of(rest.begin(), rest.end(), [](unsigned char c) {
                   return std::isdigit(c);
               }))
            return std::nullopt; // "cores:" line present but not a bare int
        try {
            return std::stoi(rest);
        } catch (const std::exception&) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

} // namespace calango::core
