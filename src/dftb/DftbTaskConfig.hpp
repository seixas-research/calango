#pragma once

#include "dft/DftTypes.hpp"

#include <array>
#include <string>
#include <vector>

/// The plain-text task manifest `calango-dftb-run` reads.
///
/// NOT JSON — matching `core::ElectronPhononIo`'s own manifest, and for the
/// SAME reason its doc comment gives: "so that this module... stay[s] free
/// of Qt". `calango-dftb-run` links only CALANGO_DFTB_SOURCES plus the
/// Qt-free parts of src/core (Structure, Element, UnitCell) — no
/// QJsonDocument, no Qt at all. RESULTS are still written as real JSON (the
/// viewers need that), by hand-formatted `std::ofstream <<`, exactly like
/// `ElectronPhononIo::writeElectronPhononResult` — writing flat/nested JSON
/// by hand is safe; parsing arbitrary JSON without a library is not, which
/// is the actual reason the input side avoids it.
///
/// Format: one `key value...` per line, `#` starts a comment, blank lines
/// ignored. Emitted by the thin Python wrapper AseScriptGenerator.cpp writes
/// for CalculatorKind::CalangoDftb (see EngineCalculatorBlocks.cpp) — the
/// wrapper exports the ASE `atoms` to `structure.extxyz` via ordinary
/// `ase.io.write`, writes this manifest next to it, then execs
/// `calango-dftb-run <manifest>` as a subprocess and relays its stdout
/// (the CALANGO_* markers) line for line.
namespace calango::dftb {

enum class DftbTask {
    SinglePoint,
    Bands,
    Pdos,
    Unfolding,
    Optics,
};

struct DftbTaskConfig {
    DftbTask task = DftbTask::SinglePoint;
    std::string structurePath;  ///< extxyz, ASE-written
    std::string skDirectory;
    bool scc = true;
    double sccToleranceElectrons = 1.0e-5;
    int maxSccIterations = 100;
    double fillingTemperatureHartree = 0.0;
    double mixingParameter = 0.2;
    bool andersonMixing = true;

    /// SCF Brillouin-zone mesh (Monkhorst-Pack, Gamma-centred, reduced by
    /// time reversal) — used for SinglePoint, Pdos and as the baseline
    /// density for Unfolding/Optics. Ignored (Gamma only) for a
    /// non-periodic structure.
    std::array<int, 3> kMesh{{4, 4, 4}};

    /// Bands: path to a k-point list, one "kx ky kz [label]" per line,
    /// fractional coordinates — the SAME convention every other engine's
    /// band-structure generator already uses for its own k-path file.
    std::string kPathFile;
    /// How many bands above/below the Fermi level to keep in bands.json,
    /// matching TwoDBandsScriptGenerator's own selection window.
    int bandsBelow = 6;
    int bandsAbove = 6;

    /// Pdos: broadening (Hartree) and bin width (Hartree) for the sampled
    /// histogram — the tetrahedron method is not implemented for this
    /// engine (see FUTURE.md); every DFTB PDOS is the sampling/broadened
    /// kind, "integration": "sampling" in the JSON, matching one of the
    /// two conventions the shared PDOS viewer already reads.
    double pdosBroadeningHartree = 0.01;
    double pdosBinWidthHartree = 0.005;

    /// Unfolding: the PRIMITIVE cell `structure` (the supercell) is being
    /// unfolded against — read the same way `structure` is (extxyz).
    /// `kPathFile` above is reused, but its k-points are PRIMITIVE
    /// fractional coordinates this time, not supercell ones.
    std::string primitiveStructurePath;
    /// Passed straight through into effective_bands.json for
    /// core::computeSpectralFunction() to consume client-side — plot-range
    /// hints, not physics; matching core::SpectralFunctionOptions's own
    /// defaults.
    double unfoldingEnergyMinEv = -10.0;
    double unfoldingEnergyMaxEv = 10.0;
    int unfoldingEnergyBins = 400;
    double unfoldingSigmaEv = 0.05;
    double unfoldingWeightThreshold = 1.0e-4;

    /// Optics: a linear frequency grid [0, omegaMaxEv] with omegaSteps + 1
    /// points, plus the broadening/direction/2D knobs DftbOpticsOptions
    /// takes directly — see DftbOptics.hpp for what each means.
    double opticsOmegaMaxEv = 20.0;
    int opticsSteps = 200;
    double opticsBroadeningEv = 0.1;
    int opticsDirection = 0; ///< 0=x, 1=y, 2=z
    /// > 0 for a 2D-periodic structure: reports the per-area sheet
    /// observables (alpha_2D, absorbance, sigma_2D) alongside eps(w),
    /// using this vacuum-direction cell length (Angstrom).
    double opticsVacuumThicknessAngstrom = 0.0;

    std::string outputPath; ///< result JSON file, e.g. "bands.json"

    /// Free-form key/value pairs the parser did not recognise — surfaced so
    /// a typo in the manifest is reported, not silently ignored.
    std::vector<std::pair<std::string, std::string>> unknownKeys;
};

dft::Outcome parseDftbTaskConfig(const std::string& text, DftbTaskConfig& out);
dft::Outcome loadDftbTaskConfig(const std::string& path, DftbTaskConfig& out);

} // namespace calango::dftb
