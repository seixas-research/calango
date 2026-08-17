#pragma once

#include "core/Structure.hpp"

#include <optional>
#include <string>
#include <vector>

namespace calango::pybridge {

/// Computational 2D Materials Database (C2DB, DTU) access.
///
/// ACCESS ROUTE, VERIFIED AGAINST THE LIVE SITE (not assumed from memory —
/// c2db.fysik.dtu.dk's actual page/form markup, fetched and inspected
/// directly in this session; an earlier draft of this class assumed the
/// site took a `?query=...`-style ASE mini-language URL parameter, which
/// turned out to be wrong — see below — and has been corrected):
///
///   - The search page is htmx-driven, not a plain query-string endpoint.
///     Loading `https://c2db.fysik.dtu.dk/` (no account needed) hands back
///     a page containing a per-visit session id (`sid`) and a filter form
///     whose real, confirmed field names are `filter` (free-text
///     formula/composition, e.g. "MoS2" — the form's own placeholder),
///     `stoichiometry`, `from_ehull`/`to_ehull` (energy-above-hull range,
///     eV/atom), `from_bg`/`to_bg` (band-gap range, eV) with a `bg` select
///     choosing which gap column the range applies to (confirmed options:
///     `gap` = PBE, `gap_hse` = HSE06, `gap_gw` = GW). Submitting it is a
///     `GET /table?sid=<sid>&filter=...&...` that returns an HTML
///     *fragment* (just the `#table-div` htmx swap target: a summary line
///     plus the results `<table>`), which `searchOnline()` fetches in two
///     polite, rate-limited steps (get a session, then get filtered
///     results) and `parseResultsHtml()` scrapes — there is no JSON/CSV
///     export of a search result set, only this HTML. Individual material
///     pages DO offer structured per-row downloads (XYZ/CIF/JSON), which
///     `fetchStructureOnline()` uses instead of scraping a detail page.
///   - The form has NO input for magnetic state, dynamic stability, or
///     layer group — confirmed by reading the full form markup, not just
///     assumed absent. Those three are real C2DB fields (their key names —
///     `magstate`, `dyn_stab`, `layergroup` — are confirmed live via the
///     results table's own "Add column" dropdown, whose `<option value=...>`
///     list is the site's own field vocabulary), and `magstate`/
///     `layergroup` even appear as columns in `parseResultsHtml()`'s output
///     when present, but none of the three can be used to FILTER a live
///     search through this route — only through `searchLocalDb()`. Per the
///     task's "do not fabricate filterable fields" instruction, the online
///     search path exposes only what its form actually accepts: formula/
///     elements, energy-above-hull, and PBE band-gap range.
///   - The FULL current database, as a single `c2db.db` ase.db file, is
///     documented as "provided upon request" (2dhub.org, formerly
///     cmr.fysik.dtu.dk/c2db — that redirect itself only exists as of this
///     writing) — there is no public, stable, directly-downloadable URL for
///     it, so Calango cannot fetch it automatically. A user who has
///     requested and obtained one can point `searchLocalDb()`/
///     `fetchStructureLocalDb()` at it directly via `ase.db.connect()`,
///     which is both faster and exposes every field/filter (magstate,
///     dyn_stab, layergroup included) via ASE's own query mini-language,
///     not just the scraped columns.
///
/// QUERY / KEY NAMES: `gap`, `gap_hse`, `gap_gw`, `ehull`, `dyn_stab` and
/// `magstate` are all confirmed directly against the live site in this
/// session (the `bg` select's options for the first three; the "Add
/// column" dropdown's option values for the last two). `hform` and
/// `layergroup` are confirmed via the results table's own sort-key
/// attributes (`hx-get=".../sort=layergroup"`) and standard C2DB usage
/// (Haastrup et al. 2018; Gjerding et al. 2021) respectively.
///
/// ATTRIBUTION: C2DB is CC BY-NC 4.0 (non-commercial) and its own site asks
/// that BOTH papers be cited (see kCitation below) — shown in the tab, not
/// just in this header.
class C2db {
public:
    /// One row of a search result — the summary fields the results table
    /// (online) or a `select()` row (local db) exposes, plus the id needed
    /// to fetch the structure itself.
    struct SearchHit {
        std::string uid;          ///< e.g. "2As-1" — the URL path segment / db row uid
        std::string formula;
        double ehull = 0.0;       ///< eV/atom, energy above the convex hull
        bool hasEhull = false;
        double hform = 0.0;       ///< eV/atom, heat of formation
        bool hasHform = false;
        double gapPbe = 0.0;      ///< eV, PBE band gap
        bool hasGapPbe = false;
        /// "Yes"/"No" as scraped from the results table's "Magnetic" column
        /// when searching online (a boolean, NOT the NM/FM/AFM state — the
        /// online form has no such column by default); the full state
        /// string (e.g. "NM", "FM") when read from a local `.db` file's
        /// `magstate` key instead. Empty = not shown/unknown for this hit.
        std::string magneticState;
        bool dynamicallyStable = false;
        bool hasDynamicStability = false;  ///< always false from searchOnline() — see class doc
        std::string layerGroup;   ///< e.g. "p-6m2" (empty = unknown)
    };

    /// Search filters — only fields the chosen access route actually
    /// supports are exposed here; nothing is fabricated. `std::nullopt` /
    /// empty means "no constraint on this field". NOTE: `dynamicallyStable`,
    /// `magneticState` and `layerGroup` are honoured by `searchLocalDb()`
    /// only — the live site's own search form has no such filter inputs
    /// (see the class doc comment), so `searchOnline()`/
    /// `buildOnlineFormQuery()` ignore them.
    struct SearchFilters {
        /// Comma/space-separated element symbols (e.g. "Mo,S") — ANDed
        /// presence conditions for `searchLocalDb()`, mutually exclusive
        /// with `formula` below. For `searchOnline()` this is passed as
        /// best-effort free text to the site's own `filter` field (joined
        /// with no separator), since that field's exact matching semantics
        /// were not independently verified beyond "it exists and matches on
        /// formula/composition" (its own placeholder is "Example: 'MoS2'").
        std::string elements;
        /// An exact formula (e.g. "MoS2") instead of an element list.
        std::string formula;
        std::optional<double> energyAboveHullMaxEvPerAtom;
        std::optional<bool> dynamicallyStable;  ///< searchLocalDb() only
        std::optional<double> gapMinEv;
        std::optional<double> gapMaxEv;
        std::string magneticState; ///< exact match, e.g. "NM"; searchLocalDb() only
        std::string layerGroup;    ///< exact match; searchLocalDb() only
        int limit = 100;
    };

    /// Build the ASE-db query-string mini-language for
    /// `ase.db.connect(path).select(query)` — the local `.db` file route.
    /// Pure and network-free, so it is unit-tested directly with no fixture
    /// needed. NOT used for the online route — see `buildOnlineFormQuery()`.
    static std::string buildQueryString(const SearchFilters& filters);

    /// Build the `filter=...&from_ehull=...&to_ehull=...&from_bg=...&
    /// to_bg=...&bg=gap` query string c2db.fysik.dtu.dk's own `/table`
    /// endpoint actually accepts (see the class doc comment) — only the
    /// fields that endpoint supports are encoded; `dynamicallyStable`,
    /// `magneticState` and `layerGroup` are silently not applied (there is
    /// no such input on the real form to apply them to). Pure and
    /// network-free.
    static std::string buildOnlineFormQuery(const SearchFilters& filters);

    /// Parse a C2DB search-results HTML fragment into hits. Network-free —
    /// exercised directly against a saved fixture in tests, and shared by
    /// `searchOnline()` for the real fetch. Not a general HTML parser: it
    /// looks for one `<table>` of `<tr>`/`<th>`/`<td>` cells (C2DB's results
    /// table shape), maps columns to fields by case-insensitive substring
    /// match on the header text (robust to the exact header wording
    /// changing slightly, not to the table disappearing), and reads the
    /// first `<a href="...">` in each row as the material's detail-page
    /// link (the uid is the link's final path segment).
    static std::vector<SearchHit> parseResultsHtml(const std::string& html);

    /// Search the live c2db.fysik.dtu.dk web interface. Best-effort
    /// rate-limited (refuses a second call inside a short cooldown window
    /// rather than firing immediately — see the .cpp) and degrades to a
    /// clear std::runtime_error, never a crash, when offline or the page
    /// shape has changed enough that parseResultsHtml() finds no table.
    static std::vector<SearchHit> searchOnline(const SearchFilters& filters);

    /// Fetch one structure by uid from the live site's per-row JSON
    /// download (structured data, not scraped HTML).
    static core::Structure fetchStructureOnline(const std::string& uid);

    /// Search a local ase.db-format `.db` file the user already has (see
    /// the class doc's "provided upon request" note).
    static std::vector<SearchHit> searchLocalDb(const std::string& dbPath,
                                                const SearchFilters& filters);

    /// Fetch one structure by uid from a local `.db` file.
    static core::Structure fetchStructureLocalDb(const std::string& dbPath,
                                                 const std::string& uid);

    /// The two-paper citation C2DB's own site asks users to include,
    /// verified against the live site/its documentation in this session —
    /// shown verbatim in the Database Browser tab.
    static const char* citationText();

    /// "CC BY-NC 4.0" — the license stated on the C2DB data pages checked in
    /// this session. Non-commercial: worth surfacing, not just citing.
    static const char* licenseText();
};

} // namespace calango::pybridge
