#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace calango::core {

struct CalphadScriptConfig {
    /// Path to the `.tdb`, relative to the job directory.
    std::string databaseFile = "assessment.tdb";
    /// Chemical components. VA is appended automatically — pycalphad requires
    /// it whenever any phase's model names it, which in a real database is
    /// almost always, and omitting it produces an error message about
    /// "components not in database" that points nowhere near the cause.
    std::vector<std::string> components;
    /// Phases to let compete. Empty means every phase in the database.
    std::vector<std::string> phases;
    /// The composition axis: the component whose mole fraction is scanned.
    std::string axisElement;

    double minTemperatureK = 300.0;
    double maxTemperatureK = 2000.0;
    double temperatureStepK = 10.0;
    double compositionStep = 0.02;
    double pressurePa = 101325.0;

    /// Ternary isothermal section instead of a binary T-x map. `axisElement`
    /// and `secondAxisElement` are then the two scanned fractions and the
    /// temperature is fixed at `sectionTemperatureK`.
    bool ternary = false;
    std::string secondAxisElement;
    double sectionTemperatureK = 1000.0;

    std::string resultsJson = "calphad_equilibrium.json";
};

/// A standalone script that computes CALPHAD equilibria with pycalphad.
///
/// WHY THIS EXISTS AT ALL, given that Calango draws its own phase diagrams in
/// C++ without a solver. The C++ construction is a convex hull over sampled
/// Gibbs curves, which is exact for what it models and models a real subset:
/// substitutional phases, no magnetic term, no internal sublattice
/// equilibria. pycalphad models all of it. So this script is the way to take a
/// database Calango cannot fully evaluate — or to check one it can — and it is
/// the ONLY place in the CALPHAD module that pycalphad appears.
///
/// pycalphad is installed in no Calango environment by default, and that is
/// the constraint the whole module is built around: nothing may import it at
/// load time. Here it is imported inside the generated script, at run time,
/// inside a try/except that reports its own absence with the command that
/// fixes it. The script is otherwise self-contained standard-library Python
/// and imports nothing from Calango.
class CalphadScriptGenerator {
public:
    static std::string generate(const CalphadScriptConfig& config);
};

} // namespace calango::core
