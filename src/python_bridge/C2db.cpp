#include "python_bridge/C2db.hpp"

#include "python_bridge/AseBridge.hpp"

#include <pybind11/embed.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace py = pybind11;

namespace calango::pybridge {

namespace {

std::string toLower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

/// Split "Mo,S" / "Mo S" / "Mo, S" into element tokens, same tolerant
/// separators as the Materials Project tab's own element-list parsing.
std::vector<std::string> splitTokens(const std::string& raw)
{
    std::vector<std::string> out;
    std::string current;
    for (char c : raw) {
        if (std::isspace(static_cast<unsigned char>(c)) || c == ',' || c == '-' || c == ';'
            || c == '/') {
            if (!current.empty()) {
                out.push_back(current);
                current.clear();
            }
        } else {
            current += c;
        }
    }
    if (!current.empty())
        out.push_back(current);
    return out;
}

std::string formatDouble(double v)
{
    std::ostringstream out;
    out << v;
    return out.str();
}

/// Best-effort politeness: refuse to fire two live requests within this
/// window of each other. A single static timestamp is enough for a
/// single-user desktop app hitting one host from the GUI thread.
std::chrono::steady_clock::time_point& lastRequestTime()
{
    static std::chrono::steady_clock::time_point t{};
    return t;
}

void waitForPoliteInterval()
{
    constexpr auto kMinInterval = std::chrono::milliseconds(1500);
    const auto now = std::chrono::steady_clock::now();
    const auto elapsed = now - lastRequestTime();
    if (elapsed < kMinInterval)
        std::this_thread::sleep_for(kMinInterval - elapsed);
    lastRequestTime() = std::chrono::steady_clock::now();
}

/// Strip tags from one cell's inner HTML to get its visible text, and
/// collapse whitespace — good enough for a simple data-table cell (no
/// nested tables, no <script> content), not a general HTML-to-text engine.
std::string stripTags(const std::string& html)
{
    std::string out;
    bool inTag = false;
    for (char c : html) {
        if (c == '<')
            inTag = true;
        else if (c == '>')
            inTag = false;
        else if (!inTag)
            out += c;
    }
    // Collapse whitespace/newlines and a few common entities.
    std::string collapsed;
    bool lastSpace = false;
    for (char c : out) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!lastSpace)
                collapsed += ' ';
            lastSpace = true;
        } else {
            collapsed += c;
            lastSpace = false;
        }
    }
    while (!collapsed.empty() && collapsed.front() == ' ')
        collapsed.erase(collapsed.begin());
    while (!collapsed.empty() && collapsed.back() == ' ')
        collapsed.pop_back();
    // A handful of entities common in a numeric/label table; not a full decoder.
    const std::vector<std::pair<std::string, std::string>> entities = {
        {"&amp;", "&"}, {"&lt;", "<"}, {"&gt;", ">"}, {"&nbsp;", " "}, {"&minus;", "-"},
    };
    for (const auto& [from, to] : entities) {
        std::size_t pos = 0;
        while ((pos = collapsed.find(from, pos)) != std::string::npos) {
            collapsed.replace(pos, from.size(), to);
            pos += to.size();
        }
    }
    return collapsed;
}

/// First `href=...` inside a block of HTML, or empty. Handles both a
/// quoted value (`href="/material/x"`) and C2DB's actual unquoted form
/// (`href=/material/2As-1`) — confirmed live in this session that the real
/// site emits the latter, which an earlier, quote-only version of this
/// function silently failed to read (every parsed hit had an empty uid).
std::string firstHref(const std::string& html)
{
    const std::size_t hrefPos = html.find("href=");
    if (hrefPos == std::string::npos)
        return {};
    std::size_t start = hrefPos + 5;
    if (start >= html.size())
        return {};
    const char quote = html[start];
    if (quote == '"' || quote == '\'') {
        ++start;
        const std::size_t end = html.find(quote, start);
        if (end == std::string::npos)
            return {};
        return html.substr(start, end - start);
    }
    // Unquoted: runs until whitespace, '>', or a self-closing "/>" — NOT
    // any bare '/', which a URL path (e.g. "/material/MoS2-1") is full of.
    std::size_t end = start;
    while (end < html.size() && !std::isspace(static_cast<unsigned char>(html[end]))
           && html[end] != '>'
           && !(html[end] == '/' && end + 1 < html.size() && html[end + 1] == '>'))
        ++end;
    return html.substr(start, end - start);
}

/// Split `html` into the contents of every `<TAG ...>...</TAG>` block
/// (case-insensitive tag name), not recursing into nested same-name tags.
std::vector<std::string> splitBlocks(const std::string& html, const std::string& tag)
{
    std::vector<std::string> blocks;
    const std::string openLower = "<" + tag;
    const std::string closeLower = "</" + tag;
    const std::string lowerHtml = toLower(html);
    std::size_t pos = 0;
    while (true) {
        const std::size_t openStart = lowerHtml.find(openLower, pos);
        if (openStart == std::string::npos)
            break;
        const std::size_t openEnd = lowerHtml.find('>', openStart);
        if (openEnd == std::string::npos)
            break;
        const std::size_t closeStart = lowerHtml.find(closeLower, openEnd);
        if (closeStart == std::string::npos)
            break;
        blocks.push_back(html.substr(openEnd + 1, closeStart - openEnd - 1));
        pos = lowerHtml.find('>', closeStart);
        if (pos == std::string::npos)
            break;
        ++pos;
    }
    return blocks;
}

/// Split one `<tr>...</tr>` row's HTML into its cells. C2DB's results table
/// uses plain `<td>` in some layouts but, confirmed live, its actual
/// current data rows use `<th scope="row">` for EVERY cell (not just the
/// header row) — so this tries `<td>` first and falls back to `<th>` rather
/// than assuming one or the other; a row using neither yields no cells,
/// same as before.
std::vector<std::string> splitRowCells(const std::string& rowHtml)
{
    auto cells = splitBlocks(rowHtml, "td");
    if (cells.empty())
        cells = splitBlocks(rowHtml, "th");
    return cells;
}

double toDoubleOrZero(const std::string& text, bool* ok)
{
    try {
        std::size_t consumed = 0;
        const double v = std::stod(text, &consumed);
        *ok = consumed > 0;
        return v;
    } catch (...) {
        *ok = false;
        return 0.0;
    }
}

} // namespace

std::string C2db::buildQueryString(const SearchFilters& filters)
{
    std::vector<std::string> terms;

    if (!filters.formula.empty()) {
        terms.push_back(filters.formula);
    } else if (!filters.elements.empty()) {
        for (const auto& element : splitTokens(filters.elements))
            terms.push_back(element);
    }

    if (filters.energyAboveHullMaxEvPerAtom)
        terms.push_back("ehull<" + formatDouble(*filters.energyAboveHullMaxEvPerAtom));
    if (filters.dynamicallyStable)
        terms.push_back(std::string("dyn_stab=") + (*filters.dynamicallyStable ? "1" : "0"));
    if (filters.gapMinEv)
        terms.push_back("gap>=" + formatDouble(*filters.gapMinEv));
    if (filters.gapMaxEv)
        terms.push_back("gap<=" + formatDouble(*filters.gapMaxEv));
    if (!filters.magneticState.empty())
        terms.push_back("magstate=" + filters.magneticState);
    if (!filters.layerGroup.empty())
        terms.push_back("layergroup=" + filters.layerGroup);

    std::string out;
    for (std::size_t i = 0; i < terms.size(); ++i) {
        if (i > 0)
            out += ",";
        out += terms[i];
    }
    return out;
}

std::string C2db::buildOnlineFormQuery(const SearchFilters& filters)
{
    std::vector<std::pair<std::string, std::string>> params;

    if (!filters.formula.empty()) {
        params.emplace_back("filter", filters.formula);
    } else if (!filters.elements.empty()) {
        std::string joined;
        for (const auto& element : splitTokens(filters.elements))
            joined += element;
        if (!joined.empty())
            params.emplace_back("filter", joined);
    }

    if (filters.energyAboveHullMaxEvPerAtom)
        params.emplace_back("to_ehull", formatDouble(*filters.energyAboveHullMaxEvPerAtom));
    if (filters.gapMinEv)
        params.emplace_back("from_bg", formatDouble(*filters.gapMinEv));
    if (filters.gapMaxEv)
        params.emplace_back("to_bg", formatDouble(*filters.gapMaxEv));
    // Always the PBE gap column: the only gap value SearchHit models.
    params.emplace_back("bg", "gap");

    std::string out;
    for (const auto& [key, value] : params) {
        if (!out.empty())
            out += "&";
        out += key + "=" + value;
    }
    return out;
}

std::vector<C2db::SearchHit> C2db::parseResultsHtml(const std::string& html)
{
    const auto tables = splitBlocks(html, "table");
    if (tables.empty())
        throw std::runtime_error(
            "No results table found in the C2DB search page — the site's page "
            "layout may have changed. Try the local .db file mode instead.");

    // C2DB's search page has exactly one results table; if a future layout
    // adds more, the header-driven column mapping below still degrades
    // gracefully (it just skips whichever table has no header it
    // recognises) — try each until one parses.
    for (const auto& table : tables) {
        const auto rows = splitBlocks(table, "tr");
        if (rows.size() < 2)
            continue;

        const auto headerCells = splitRowCells(rows.front());
        if (headerCells.empty())
            continue;

        enum class Field { None, Formula, Ehull, Hform, Gap, Magstate, DynStab, LayerGroup };
        std::vector<Field> columns;
        for (const auto& cell : headerCells) {
            const std::string text = toLower(stripTags(cell));
            if (text.find("formula") != std::string::npos)
                columns.push_back(Field::Formula);
            else if (text.find("hull") != std::string::npos)
                columns.push_back(Field::Ehull);
            else if (text.find("heat of formation") != std::string::npos
                     || text.find("hform") != std::string::npos)
                columns.push_back(Field::Hform);
            else if (text.find("gap") != std::string::npos && text.find("pbe") != std::string::npos)
                columns.push_back(Field::Gap);
            else if (text.find("gap") != std::string::npos
                     && text.find("hse") == std::string::npos
                     && text.find("gw") == std::string::npos)
                columns.push_back(Field::Gap);
            else if (text.find("magnetic") != std::string::npos)
                columns.push_back(Field::Magstate);
            else if (text.find("dynam") != std::string::npos)
                columns.push_back(Field::DynStab);
            else if (text.find("layer") != std::string::npos)
                columns.push_back(Field::LayerGroup);
            else
                columns.push_back(Field::None);
        }
        if (std::none_of(columns.begin(), columns.end(),
                         [](Field f) { return f == Field::Formula; }))
            continue; // not the results table — no formula column at all

        std::vector<SearchHit> hits;
        for (std::size_t r = 1; r < rows.size(); ++r) {
            const auto cells = splitRowCells(rows[r]);
            if (cells.empty())
                continue;
            SearchHit hit;
            std::string firstLinkHref;
            for (std::size_t c = 0; c < cells.size() && c < columns.size(); ++c) {
                const std::string text = stripTags(cells[c]);
                if (firstLinkHref.empty())
                    firstLinkHref = firstHref(cells[c]);
                bool ok = false;
                switch (columns[c]) {
                case Field::Formula:
                    hit.formula = text;
                    break;
                case Field::Ehull: {
                    const double v = toDoubleOrZero(text, &ok);
                    hit.hasEhull = ok;
                    hit.ehull = ok ? v : 0.0;
                    break;
                }
                case Field::Hform: {
                    const double v = toDoubleOrZero(text, &ok);
                    hit.hasHform = ok;
                    hit.hform = ok ? v : 0.0;
                    break;
                }
                case Field::Gap: {
                    const double v = toDoubleOrZero(text, &ok);
                    hit.hasGapPbe = ok;
                    hit.gapPbe = ok ? v : 0.0;
                    break;
                }
                case Field::Magstate:
                    hit.magneticState = text;
                    break;
                case Field::DynStab: {
                    const std::string lower = toLower(text);
                    hit.hasDynamicStability = !text.empty();
                    hit.dynamicallyStable = lower == "yes" || lower == "true" || lower == "1"
                        || lower.find("stable") != std::string::npos;
                    break;
                }
                case Field::LayerGroup:
                    hit.layerGroup = text;
                    break;
                case Field::None:
                    break;
                }
            }
            if (hit.formula.empty())
                continue;
            // uid = final path segment of the row's own link, e.g.
            // "/material/2As-1" -> "2As-1".
            if (!firstLinkHref.empty()) {
                const std::size_t slash = firstLinkHref.find_last_of('/');
                hit.uid = slash == std::string::npos ? firstLinkHref
                                                     : firstLinkHref.substr(slash + 1);
            }
            hits.push_back(std::move(hit));
        }
        if (!hits.empty())
            return hits;
    }
    throw std::runtime_error(
        "The C2DB results table did not contain a recognisable formula column "
        "— the site's page layout may have changed.");
}

std::vector<C2db::SearchHit> C2db::searchOnline(const SearchFilters& filters)
{
    const std::string formParams = buildOnlineFormQuery(filters);
    // One logical search = two HTTP requests against the real site (get a
    // session id, then fetch the filtered results fragment for it — see
    // the class doc comment on why this replaced a single-request
    // `?query=` design that turned out not to match the live form at all).
    // Rate-limit-gated once here, with a short in-Python pause between the
    // two legs, rather than gating each leg separately — the two requests
    // are one user-initiated search, not two independent ones.
    waitForPoliteInterval();
    try {
        py::dict locals;
        locals["form_params"] = formParams;
        py::exec(R"PY(
import re
import time
import urllib.error
import urllib.request

headers = {"User-Agent": "calango (materials modeling GUI; polite, "
                        "rate-limited, two requests per search: session "
                        "then filtered results)"}

def _get(url):
    request = urllib.request.Request(url, headers=headers)
    try:
        with urllib.request.urlopen(request, timeout=20) as response:
            return response.read().decode("utf-8", errors="replace")
    except urllib.error.HTTPError as error:
        raise RuntimeError(f"C2DB search failed (HTTP {error.code}) for {url}.") from None
    except urllib.error.URLError as error:
        raise RuntimeError(
            f"Network error contacting C2DB: {error.reason}. C2DB search "
            "needs internet access — if you have a local c2db.db file "
            "instead, use the local-file mode.") from None

# Leg 1: a plain page load allocates a fresh per-visit session id (sid),
# which the results-fragment endpoint below requires.
front_page = _get("https://c2db.fysik.dtu.dk/")
sid_match = re.search(r'name="sid"\s+value="(\d+)"', front_page)
if sid_match is None:
    raise RuntimeError(
        "Could not find a C2DB session id on the search page — the site's "
        "page layout may have changed. Try the local .db file mode instead.")
sid = sid_match.group(1)

time.sleep(0.5)

# Leg 2: the same request the site's own search form issues via htmx —
# an HTML fragment (just the results table area), not a full page.
url = f"https://c2db.fysik.dtu.dk/table?sid={sid}"
if form_params:
    url += "&" + form_params
html = _get(url)
)PY",
                 locals, locals);
        const std::string html = locals["html"].cast<std::string>();
        auto hits = parseResultsHtml(html);
        // The site paginates at a fixed page size regardless of any limit
        // we ask for (confirmed live: it ignores an unrecognised `limit`
        // param and always returns "showing rows 1-25") — clip client-side
        // so the caller's limit is still honoured.
        const std::size_t limit = static_cast<std::size_t>(std::max(1, filters.limit));
        if (hits.size() > limit)
            hits.resize(limit);
        return hits;
    } catch (const py::error_already_set& e) {
        throw std::runtime_error(std::string("C2DB search failed:\n") + e.what());
    }
}

core::Structure C2db::fetchStructureOnline(const std::string& uid)
{
    if (uid.empty())
        throw std::runtime_error("No C2DB material selected.");
    waitForPoliteInterval();
    try {
        py::dict locals;
        locals["uid"] = uid;
        // Two-step, deliberately: fetch the material page HTML and read off
        // its OWN "Download: XYZ" link rather than guessing the download
        // URL's shape — robust to the exact download-route convention,
        // which was not independently confirmed in this session (only that
        // the link exists, with XYZ/CIF/JSON options). Extended XYZ is
        // preferred over CIF/JSON here specifically because it is ASE's own
        // native round-trip format: it is the one download format
        // guaranteed to preserve the pbc flags and cell exactly as the
        // source row's own Atoms object carried them (2D pbc=[T,T,False],
        // or [T,T,T] with a vacuum gap) — the property this import path
        // most needs to get right for the downstream 2D-aware modules.
        py::exec(R"PY(
import re
import urllib.error
import urllib.request

page_url = f"https://c2db.fysik.dtu.dk/material/{uid}"
headers = {"User-Agent": "calango (materials modeling GUI; polite, "
                        "rate-limited, single request per import)"}

def _get(url):
    request = urllib.request.Request(url, headers=headers)
    try:
        with urllib.request.urlopen(request, timeout=20) as response:
            return response.read().decode("utf-8", errors="replace")
    except urllib.error.HTTPError as error:
        raise RuntimeError(f"C2DB request failed (HTTP {error.code}) for {url}.") from None
    except urllib.error.URLError as error:
        raise RuntimeError(f"Network error contacting C2DB: {error.reason}") from None

page_html = _get(page_url)
# Any <a href="..."> whose href or link text mentions xyz, case-insensitive.
link_match = re.search(
    r'<a[^>]+href="([^"]+)"[^>]*>[^<]*xyz[^<]*</a>', page_html, re.IGNORECASE)
if link_match is None:
    link_match = re.search(
        r'<a[^>]+href="([^"]*xyz[^"]*)"', page_html, re.IGNORECASE)
if link_match is None:
    raise RuntimeError(
        f"No XYZ download link found on the C2DB page for '{uid}' — the "
        "site's page layout may have changed.")
xyz_url = link_match.group(1)
if xyz_url.startswith("/"):
    xyz_url = "https://c2db.fysik.dtu.dk" + xyz_url
elif not xyz_url.startswith("http"):
    xyz_url = page_url.rstrip("/") + "/" + xyz_url

xyz_text = _get(xyz_url)

import io
import ase.io
atoms = ase.io.read(io.StringIO(xyz_text), format="extxyz")
)PY",
                 locals, locals);
        return AseBridge::fromAtoms(locals["atoms"]);
    } catch (const py::error_already_set& e) {
        throw std::runtime_error(std::string("C2DB fetch failed:\n") + e.what());
    }
}

std::vector<C2db::SearchHit> C2db::searchLocalDb(const std::string& dbPath,
                                                 const SearchFilters& filters)
{
    if (dbPath.empty())
        throw std::runtime_error("Choose a local c2db.db file first.");
    const std::string query = buildQueryString(filters);
    try {
        py::dict locals;
        locals["db_path"] = dbPath;
        locals["query"] = query;
        locals["limit"] = std::max(1, std::min(filters.limit, 5000));
        py::exec(R"PY(
import ase.db

db = ase.db.connect(db_path)
hits = []
for row in db.select(query if query else None, limit=limit):
    key_values = row.key_value_pairs
    hits.append({
        "uid": str(getattr(row, "uid", row.id)),
        "formula": row.formula,
        "ehull": float(key_values.get("ehull")) if key_values.get("ehull") is not None else 0.0,
        "has_ehull": key_values.get("ehull") is not None,
        "hform": float(key_values.get("hform")) if key_values.get("hform") is not None else 0.0,
        "has_hform": key_values.get("hform") is not None,
        "gap": float(key_values.get("gap")) if key_values.get("gap") is not None else 0.0,
        "has_gap": key_values.get("gap") is not None,
        "magstate": str(key_values.get("magstate") or ""),
        "dyn_stab": bool(key_values.get("dyn_stab")),
        "has_dyn_stab": key_values.get("dyn_stab") is not None,
        "layergroup": str(key_values.get("layergroup") or ""),
    })
)PY",
                 locals, locals);
        std::vector<SearchHit> results;
        for (const auto& item : locals["hits"].cast<py::list>()) {
            const auto row = item.cast<py::dict>();
            SearchHit hit;
            hit.uid = row["uid"].cast<std::string>();
            hit.formula = row["formula"].cast<std::string>();
            hit.ehull = row["ehull"].cast<double>();
            hit.hasEhull = row["has_ehull"].cast<bool>();
            hit.hform = row["hform"].cast<double>();
            hit.hasHform = row["has_hform"].cast<bool>();
            hit.gapPbe = row["gap"].cast<double>();
            hit.hasGapPbe = row["has_gap"].cast<bool>();
            hit.magneticState = row["magstate"].cast<std::string>();
            hit.dynamicallyStable = row["dyn_stab"].cast<bool>();
            hit.hasDynamicStability = row["has_dyn_stab"].cast<bool>();
            hit.layerGroup = row["layergroup"].cast<std::string>();
            results.push_back(std::move(hit));
        }
        return results;
    } catch (const py::error_already_set& e) {
        throw std::runtime_error(std::string("C2DB local database search failed:\n") + e.what());
    }
}

core::Structure C2db::fetchStructureLocalDb(const std::string& dbPath, const std::string& uid)
{
    if (dbPath.empty())
        throw std::runtime_error("Choose a local c2db.db file first.");
    if (uid.empty())
        throw std::runtime_error("No C2DB material selected.");
    try {
        py::dict locals;
        locals["db_path"] = dbPath;
        locals["uid"] = uid;
        py::exec(R"PY(
import ase.db

db = ase.db.connect(db_path)
try:
    row = db.get(uid=uid)
except KeyError:
    # Fall back to the integer row id, in case `uid` is actually one.
    row = db.get(id=int(uid))
atoms = row.toatoms()
)PY",
                 locals, locals);
        return AseBridge::fromAtoms(locals["atoms"]);
    } catch (const py::error_already_set& e) {
        throw std::runtime_error(std::string("C2DB local database fetch failed:\n") + e.what());
    }
}

const char* C2db::citationText()
{
    return "S. Haastrup et al., \"The Computational 2D Materials Database: "
          "High-Throughput Modeling and Discovery of Atomically Thin "
          "Crystals\", 2D Materials 5, 042002 (2018), "
          "doi:10.1088/2053-1583/aacfc1.\n"
          "M. N. Gjerding et al., \"Recent Progress of the Computational 2D "
          "Materials Database (C2DB)\", 2D Materials 8, 044002 (2021), "
          "doi:10.1088/2053-1583/ac1059.";
}

const char* C2db::licenseText()
{
    return "CC BY-NC 4.0 (non-commercial) — c2db.fysik.dtu.dk";
}

} // namespace calango::pybridge
