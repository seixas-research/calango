// Every icon name the UI asks for must actually be bundled.
//
// IconManager resolves a name to `:/assets/icons/<name>.svg` and returns a NULL
// pixmap when the asset is missing — so a typo, or a name copied out of the
// RemixIcon source tree without bundling the file, produces a button that is
// simply blank at run time. Nothing fails at build time and nothing fails in
// the other icon test, which bundles one asset of its own and checks tinting.
//
// That is exactly how "Phase colors…" shipped with a missing glyph: the name
// `shapes-fill` was taken from RemixIcon/Design/, where the SVG does exist,
// but assets/icons/ is the curated subset and the file was never copied there.
//
// Two invariants, because either alone leaves the hole open:
//   1. Every name used in the sources has a file in assets/icons/.
//   2. Every file in assets/icons/ is listed in CMakeLists.txt — the qt_add_resources
//      list is the registry, and a file on disk that is not in it does not
//      reach the binary.
//
// Deliberately Qt-free: it is text processing over the source tree, and making
// it need a QApplication would only make it harder to run.

#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <set>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

namespace {

int failures = 0;

void check(bool condition, const std::string& what)
{
    std::cout << (condition ? "  ok   " : "  FAIL ") << what << '\n';
    if (!condition)
        ++failures;
}

std::string readFile(const fs::path& path)
{
    std::ifstream in(path);
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

/// Icon names appearing as string literals in `source`.
///
/// Two passes, because neither alone sees everything:
///
///   1. Names handed DIRECTLY to IconManager. Catches any name shape, but
///      misses the common case in this codebase — panels build their buttons
///      through a local `makeDialogButton(icon, tip)` lambda, so the literal
///      never appears in the same expression as the call.
///
///   2. Literals with the RemixIcon suffix convention (`…-fill` / `…-line`).
///      Catches the lambda case, and it is specific enough to be safe: of the
///      56 such literals currently in src/, every one is an icon name. Matching
///      all hyphenated strings instead would drag in settings keys and CSS
///      fragments, and a test that reports those as missing icons is one nobody
///      keeps passing.
///
/// A name that is neither passed directly nor suffixed (`hashtag`, `delete`) is
/// invisible to both. Accepted: the alternative is false positives, and those
/// names are covered by the CMake-registry half of this test once bundled.
void collectIconNames(const std::string& source, std::set<std::string>& names)
{
    // Custom raw-string delimiters: the patterns contain `)"`, which closes the
    // default R"( … )" form early.
    static const std::regex kCall(
        R"rx(IconManager::(?:bind|icon|pixmap|has)\s*\([^;]*?)rx"
        R"rx((?:QStringLiteral\s*\(\s*)?"([a-z0-9]+(?:-[a-z0-9]+)*)")rx",
        std::regex::optimize);
    static const std::regex kSuffixed(
        R"rx("([a-z0-9]+(?:-[a-z0-9]+)*-(?:fill|line))")rx",
        std::regex::optimize);

    for (const std::regex* pattern : {&kCall, &kSuffixed}) {
        for (auto it = std::sregex_iterator(source.begin(), source.end(),
                                            *pattern);
             it != std::sregex_iterator(); ++it) {
            names.insert((*it)[1].str());
        }
    }
}

} // namespace

int main()
{
    const fs::path root = CALANGO_SOURCE_DIR;
    const fs::path iconDir = root / "assets" / "icons";

    if (!fs::is_directory(iconDir)) {
        std::cerr << "assets/icons not found under " << root << '\n';
        return EXIT_FAILURE;
    }

    // -- What is on disk ----------------------------------------------------
    std::set<std::string> bundled;
    for (const auto& entry : fs::directory_iterator(iconDir)) {
        if (entry.path().extension() == ".svg")
            bundled.insert(entry.path().stem().string());
    }
    std::cout << "assets/icons holds " << bundled.size() << " glyphs.\n";
    check(!bundled.empty(), "the icon directory is not empty");

    // -- What the sources ask for -------------------------------------------
    std::set<std::string> requested;
    for (const auto& entry : fs::recursive_directory_iterator(root / "src")) {
        if (entry.path().extension() != ".cpp"
            && entry.path().extension() != ".hpp")
            continue;
        collectIconNames(readFile(entry.path()), requested);
    }
    std::cout << "the sources request " << requested.size()
              << " distinct icon names.\n";
    check(requested.size() > 20,
          "the scan actually found the icon call sites");

    std::cout << "Every requested icon is bundled:\n";
    for (const std::string& name : requested) {
        check(bundled.count(name) > 0,
              "assets/icons/" + name + ".svg exists (requested in src/)");
    }

    // -- What CMake registers ------------------------------------------------
    //
    // A file present in assets/icons/ but absent from the qt_add_resources list
    // never reaches the binary, so it fails at run time exactly as a missing
    // file does — with the extra confusion that the SVG is right there.
    const std::string cmake = readFile(root / "CMakeLists.txt");
    std::cout << "Every bundled icon is registered in CMakeLists.txt:\n";
    for (const std::string& name : bundled) {
        check(cmake.find("assets/icons/" + name + ".svg") != std::string::npos,
              "assets/icons/" + name + ".svg is listed for qt_add_resources");
    }

    std::cout << (failures == 0 ? "\nAll icon-registry checks passed.\n"
                                : "\nchecks FAILED.\n");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
