#pragma once

#include <algorithm>
#include <cctype>
#include <string>

namespace calango::core {

/// Upper-cased copy of `text`, byte by byte.
///
/// This is deliberately plain std::toupper on unsigned char: the callers are
/// format keywords (TDB phase and element names, CIF tags), which are ASCII
/// by specification, so no Unicode-aware casing is wanted.
inline std::string upperCase(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return text;
}

/// `text` without leading or trailing spaces, tabs, CR or LF.
///
/// Note the set includes '\n': this is a whole-token trim. Per-line emitters
/// that must preserve line structure (EngineCalculatorBlocks) keep their own
/// variant that leaves '\n' alone — do not switch them to this one.
inline std::string trimmed(const std::string& text)
{
    const auto begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos)
        return {};
    const auto end = text.find_last_not_of(" \t\r\n");
    return text.substr(begin, end - begin + 1);
}

} // namespace calango::core
