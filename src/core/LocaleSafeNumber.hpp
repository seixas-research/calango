#pragma once

#include <charconv>
#include <cstdint>
#include <string>
#include <string_view>
#include <system_error>

namespace calango::core {

/// Locale-independent number formatting and parsing for FILE FORMATS.
///
/// THE RULE: a number written into or read out of a file always uses a DOT for
/// the decimal separator, whatever the user's locale. Data files are not
/// prose; a `.tdb`, an mmCIF, an extxyz and a JSON document are all specified
/// in terms of `.` and nothing else may appear in them.
///
/// THIS EXISTS BECAUSE OF A BUG THAT REACHED A WRITTEN DATABASE. `printf("%g")`
/// and `std::stod` both follow LC_NUMERIC, and Qt sets LC_NUMERIC from the
/// environment when a QApplication is constructed. On any machine whose locale
/// uses a decimal comma — pt_BR, de_DE, fr_FR, most of Europe and South
/// America — the CALPHAD module therefore wrote
///
///     PARAMETER L(FCC_A1,AG,AU;0) 298,14999999999998 +17999,999999999996; …
///
/// which is not a `.tdb`: the comma is the sublattice separator, so every
/// number became two tokens and the file was silently unreadable by this
/// project's own parser and by every other CALPHAD tool. The same fault ran in
/// reverse on the way in, where `std::stod("298.15")` returns 298 under a
/// comma locale, quietly truncating every temperature limit in a database the
/// user imported — and, in the mmCIF reader, every atomic coordinate.
///
/// It was invisible in the unit tests, which construct no QApplication and so
/// keep the C locale, and visible only in the GUI test that does — which is
/// exactly the sort of bug that ships.
///
/// WHAT IS AND IS NOT AFFECTED, measured rather than assumed:
///
///   printf / snprintf / sprintf  ... LOCALE-DEPENDENT — never use for files
///   std::stod / stof / atof / strtod  LOCALE-DEPENDENT — never use for files
///   QString::asprintf            ... safe (Qt formats via the C locale)
///   QString::number              ... safe
///   QTextStream << double        ... safe (defaults to QLocale::c())
///   std::ostringstream << double ... safe (the global std::locale stays "C";
///                                    setlocale does not touch it)
///   QLocale::toString            ... LOCALE-DEPENDENT BY DESIGN — correct for
///                                    text shown to a user, wrong for a file
///
/// std::to_chars / std::from_chars are the fix rather than a workaround: they
/// are defined to be locale-independent, and to_chars additionally produces
/// the SHORTEST representation that round-trips exactly, so "298.15" is
/// written as "298.15" and not as "298.14999999999998".

/// `value` as text that reads back as exactly the same double, always with a
/// '.' decimal point.
inline std::string localeSafeFormat(double value)
{
    char buffer[64];
    const auto result =
        std::to_chars(buffer, buffer + sizeof(buffer), value);
    if (result.ec != std::errc())
        return "0";
    return std::string(buffer, result.ptr);
}

/// Parse a number written with a '.' decimal point, whatever the locale.
///
/// A leading '+' is skipped before the conversion: from_chars rejects it (the
/// grammar it implements is strtod's without the sign for the "general"
/// format), and TDB files do write `+1.5E-3`.
inline bool localeSafeParse(std::string_view text, double* out)
{
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
        text.remove_prefix(1);
    if (!text.empty() && text.front() == '+')
        text.remove_prefix(1);
    if (text.empty())
        return false;
    double value = 0.0;
    const auto result =
        std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc())
        return false;
    *out = value;
    return true;
}

/// Same, with a fallback for text that is not a number at all.
inline double localeSafeToDouble(std::string_view text, double fallback = 0.0)
{
    double value = fallback;
    return localeSafeParse(text, &value) ? value : fallback;
}

} // namespace calango::core
