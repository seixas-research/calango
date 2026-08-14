#include "core/JobFailureReason.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <vector>

namespace calango::core {

namespace {

std::string trim(std::string text)
{
    const auto notSpace = [](unsigned char c) { return !std::isspace(c); };
    text.erase(text.begin(),
               std::find_if(text.begin(), text.end(), notSpace));
    text.erase(std::find_if(text.rbegin(), text.rend(), notSpace).base(),
               text.end());
    return text;
}

/// True for the "SomeError: message" line that ends a Python traceback.
///
/// Matched structurally rather than against a list of exception names: the
/// interesting ones are the RuntimeErrors the generated scripts raise
/// themselves, and a whitelist would miss every library exception that has
/// not been seen yet.
bool looksLikeException(const std::string& line, std::size_t* colon)
{
    const std::size_t pos = line.find(": ");
    if (pos == std::string::npos || pos == 0)
        return false;
    // The part before the colon must be a bare identifier, possibly dotted
    // (numpy.linalg.LinAlgError), and must end in Error or Exception — which
    // is what keeps ordinary prose containing a colon from matching.
    const std::string head = line.substr(0, pos);
    if (head.find(' ') != std::string::npos)
        return false;
    const bool named = head.size() >= 5
        && (head.compare(head.size() - 5, 5, "Error") == 0
            || (head.size() >= 9
                && head.compare(head.size() - 9, 9, "Exception") == 0));
    if (!named)
        return false;
    *colon = pos;
    return true;
}

} // namespace

std::string extractFailureReason(const std::string& log)
{
    std::vector<std::string> lines;
    {
        std::istringstream stream(log);
        std::string line;
        while (std::getline(stream, line)) {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            lines.push_back(line);
        }
    }

    // Scanned from the END. A Python traceback puts the exception last, and a
    // run that raised, caught and re-raised leaves several — the final one is
    // the one that stopped the job.
    for (std::size_t i = lines.size(); i-- > 0;) {
        const std::string line = trim(lines[i]);
        if (line.empty())
            continue;
        std::size_t colon = 0;
        if (!looksLikeException(line, &colon))
            continue;

        std::string message = trim(line.substr(colon + 2));
        // A RuntimeError raised by a generated script is usually several
        // sentences wrapped across source lines, and Python prints the
        // continuation on the FOLLOWING lines with no marker. Gather them
        // while they are plain prose, so the actionable half of the message
        // is not cut off at the first newline.
        for (std::size_t j = i + 1; j < lines.size(); ++j) {
            const std::string next = trim(lines[j]);
            if (next.empty())
                break;
            // Stop at anything that looks like a new frame or a new
            // exception rather than a continuation.
            if (next.rfind("File \"", 0) == 0 || next.rfind("Traceback", 0) == 0)
                break;
            std::size_t ignored = 0;
            if (looksLikeException(next, &ignored))
                break;
            message += ' ';
            message += next;
        }
        return trim(message);
    }
    return {};
}

} // namespace calango::core
