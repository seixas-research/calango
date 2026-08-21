#include "dftb/DftbEngine.hpp"

#include "dft/Constants.hpp"
#include "dft/KPointGrid.hpp"
#include "dft/LinearAlgebra.hpp"
#include "dftb/DftbBasis.hpp"
#include "dftb/DftbForces.hpp"
#include "dftb/DftbGamma.hpp"
#include "dftb/DftbHamiltonian.hpp"
#include "dftb/DftbOptics.hpp"
#include "dftb/DftbPdos.hpp"
#include "dftb/DftbScf.hpp"
#include "dftb/DftbStructureIo.hpp"
#include "dftb/DftbUnfolding.hpp"
#include "dftb/SlaterKosterTable.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace calango::dftb {

namespace {

void info(const std::string& message)
{
    std::printf("CALANGO_INFO %s\n", message.c_str());
    std::fflush(stdout);
}

void progress(int step, int total)
{
    std::printf("CALANGO_PROGRESS %d %d\n", step, total);
    std::fflush(stdout);
}

/// Reciprocal lattice vectors (2*pi convention), from real-space cell
/// vectors already in bohr — the SAME construction as DftbGamma.cpp's own
/// (private) helper; duplicated rather than shared across two small,
/// self-contained ~10-line functions with no other coupling.
std::array<core::Vec3, 3> reciprocalVectors(const std::array<core::Vec3, 3>& a)
{
    const double volume = a[0].dot(a[1].cross(a[2]));
    std::array<core::Vec3, 3> b{};
    if (std::fabs(volume) < 1.0e-12)
        return b;
    b[0] = a[1].cross(a[2]) * (2.0 * dft::kPi / volume);
    b[1] = a[2].cross(a[0]) * (2.0 * dft::kPi / volume);
    b[2] = a[0].cross(a[1]) * (2.0 * dft::kPi / volume);
    return b;
}

dft::Outcome buildScfKMesh(const core::Structure& structure,
                            const std::array<int, 3>& divisions,
                            std::vector<DftbKPoint>& out)
{
    std::vector<std::array<double, 3>> lattice;
    const auto pbc = structure.cell().pbc();
    if (pbc[0] || pbc[1] || pbc[2]) {
        for (const auto& v : structure.cell().vectors())
            lattice.push_back({v.x * dft::kBohrPerAngstrom,
                               v.y * dft::kBohrPerAngstrom,
                               v.z * dft::kBohrPerAngstrom});
    }
    std::vector<dft::KPointGrid::Atom> atoms;
    for (const auto& atom : structure.atoms())
        atoms.push_back({atom.atomicNumber,
                         {atom.position.x * dft::kBohrPerAngstrom,
                          atom.position.y * dft::kBohrPerAngstrom,
                          atom.position.z * dft::kBohrPerAngstrom}});

    dft::KPointGrid grid;
    const auto outcome = grid.build(divisions, lattice, atoms,
                                     dft::KPointGrid::Symmetry::TimeReversal);
    if (!outcome.ok())
        return outcome;
    out.clear();
    for (const auto& kp : grid.points())
        out.push_back({kp.fractional, kp.weight});
    return dft::Outcome::success();
}

struct EngineContext {
    core::Structure structure;
    SlaterKosterTable table;
    DftbBasis basis;
    DftbHamiltonianBuilder hamiltonian;
    DftbScfSettings settings;
    std::vector<DftbKPoint> scfKPoints;
    DftbScfResult scf;
};

dft::Outcome prepareAndRunScf(const DftbTaskConfig& config, EngineContext& ctx)
{
    info("reading structure: " + config.structurePath);
    auto outcome = loadExtxyzStructure(config.structurePath, ctx.structure);
    if (!outcome.ok())
        return outcome;

    std::vector<int> atomicNumbers;
    for (const auto& atom : ctx.structure.atoms())
        atomicNumbers.push_back(atom.atomicNumber);

    info("loading Slater-Koster parameter set: " + config.skDirectory);
    outcome = ctx.table.load(config.skDirectory, atomicNumbers);
    if (!outcome.ok())
        return outcome;

    outcome = DftbBasis::build(atomicNumbers, ctx.table, ctx.basis);
    if (!outcome.ok())
        return outcome;
    info("basis: " + std::to_string(ctx.basis.totalOrbitals) + " orbitals, "
         + std::to_string(ctx.structure.atoms().size()) + " atoms");

    outcome = ctx.hamiltonian.build(ctx.structure, ctx.table, ctx.basis);
    if (!outcome.ok())
        return outcome;

    ctx.settings.sccEnabled = config.scc;
    ctx.settings.sccToleranceElectrons = config.sccToleranceElectrons;
    ctx.settings.maxSccIterations = config.maxSccIterations;
    ctx.settings.fillingTemperatureHartree = config.fillingTemperatureHartree;
    ctx.settings.mixingParameter = config.mixingParameter;
    ctx.settings.useAndersonMixing = config.andersonMixing;

    outcome = buildScfKMesh(ctx.structure, config.kMesh, ctx.scfKPoints);
    if (!outcome.ok())
        return outcome;

    info("running " + std::string(config.scc ? "SCC-DFTB (DFTB2)"
                                              : "non-SCC DFTB (DFTB0)")
         + " on " + std::to_string(ctx.scfKPoints.size()) + " k-point(s)");
    DftbScf scf;
    outcome = scf.run(ctx.structure, ctx.table, ctx.basis, ctx.hamiltonian,
                       ctx.scfKPoints, ctx.settings, ctx.scf);
    if (!outcome.ok())
        return outcome;
    if (config.scc && !ctx.scf.converged)
        info("WARNING: SCC did not converge within "
             + std::to_string(config.maxSccIterations)
             + " iterations (residual "
             + std::to_string(ctx.scf.maxChargeResidual) + ") — reporting "
             "the best-effort state");
    else if (config.scc)
        info("SCC converged in " + std::to_string(ctx.scf.iterations)
             + " iteration(s)");

    return dft::Outcome::success();
}

dft::Outcome runSinglePoint(const DftbTaskConfig& config, EngineContext& ctx)
{
    progress(1, 2);
    DftbForces forces;
    const auto outcome =
        computeDftbForces(ctx.structure, ctx.table, ctx.scfKPoints,
                           ctx.settings, forces);
    if (!outcome.ok())
        return outcome;
    progress(2, 2);

    double fmax = 0.0;
    int fmaxAtom = -1;
    for (std::size_t i = 0; i < forces.forcesEvPerAngstrom.size(); ++i) {
        const double n = forces.forcesEvPerAngstrom[i].norm();
        if (n > fmax) {
            fmax = n;
            fmaxAtom = static_cast<int>(i);
        }
    }

    std::ofstream out(config.outputPath);
    if (!out)
        return dft::Outcome::invalid("cannot write " + config.outputPath);
    out << std::setprecision(17);
    out << "{\n"
        << "  \"energy_eV\": " << ctx.scf.totalEnergyHartree * dft::kHartreeToEv
        << ",\n"
        << "  \"energy_Hartree\": " << ctx.scf.totalEnergyHartree << ",\n"
        << "  \"fermi_eV\": " << ctx.scf.fermiEnergyHartree * dft::kHartreeToEv
        << ",\n"
        << "  \"fmax_eV_per_A\": " << fmax << ",\n"
        << "  \"fmax_atom\": " << fmaxAtom << ",\n"
        << "  \"total_magnetic_moment\": null,\n"
        << "  \"magnetic_moments\": null,\n"
        << "  \"natoms\": " << ctx.structure.atoms().size() << ",\n"
        << "  \"forces_eV_per_A\": [";
    for (std::size_t i = 0; i < forces.forcesEvPerAngstrom.size(); ++i) {
        const auto& f = forces.forcesEvPerAngstrom[i];
        out << (i == 0 ? "" : ", ") << "[" << f.x << ", " << f.y << ", "
            << f.z << "]";
    }
    out << "],\n"
        << "  \"stress_eV_per_A3\": null,\n"
        << "  \"scf\": {\n"
        << "    \"completed\": " << (ctx.scf.converged ? "true" : "false")
        << ",\n"
        << "    \"iterations\": " << ctx.scf.iterations << ",\n"
        << "    \"energy_tol_eV\": " << config.sccToleranceElectrons << ",\n"
        << "    \"max_steps\": " << config.maxSccIterations << "\n"
        << "  },\n"
        << "  \"dftb\": {\n"
        << "    \"band_structure_energy_eV\": "
        << ctx.scf.bandStructureEnergyHartree * dft::kHartreeToEv << ",\n"
        << "    \"coulomb_energy_eV\": "
        << ctx.scf.coulombEnergyHartree * dft::kHartreeToEv << ",\n"
        << "    \"repulsive_energy_eV\": "
        << ctx.scf.repulsiveEnergyHartree * dft::kHartreeToEv << "\n"
        << "  }\n"
        << "}\n";
    std::printf("CALANGO_RESULT single_point=%s\n", config.outputPath.c_str());
    std::fflush(stdout);
    return dft::Outcome::success();
}

struct PathPoint {
    std::array<double, 3> fractional{};
    std::string label;
};

dft::Outcome readKPath(const std::string& path, std::vector<PathPoint>& out)
{
    std::ifstream kpathFile(path);
    if (!kpathFile)
        return dft::Outcome::invalid("cannot open kpathfile: " + path);
    out.clear();
    std::string line;
    while (std::getline(kpathFile, line)) {
        std::istringstream stream(line);
        PathPoint p;
        if (!(stream >> p.fractional[0] >> p.fractional[1] >> p.fractional[2]))
            continue;
        stream >> p.label; // optional
        out.push_back(p);
    }
    if (out.empty())
        return dft::Outcome::invalid("kpathfile has no valid k-points: " + path);
    return dft::Outcome::success();
}

/// The SCC potential shift at the CONVERGED charges, held fixed for every
/// non-self-consistent post-processing step (bands, unfolding, ...) — the
/// standard "fixed density" evaluation every other engine's own generator
/// already uses. Empty when SCC was off (H0 alone, no shift).
std::vector<double> convergedShiftFor(const EngineContext& ctx)
{
    if (!ctx.settings.sccEnabled)
        return {};
    std::vector<double> hubbardU;
    for (const auto& ao : ctx.basis.atoms) {
        const auto* shells = ctx.table.onsite(ao.atomicNumber);
        hubbardU.push_back(ao.hasP ? (*shells)[1].hubbardUHartree
                                   : (*shells)[0].hubbardUHartree);
    }
    DftbEwaldSum ewald;
    ewald.build(ctx.structure, hubbardU);
    return ewald.potentialShift(ctx.scf.deltaQ);
}

dft::Outcome runBands(const DftbTaskConfig& config, EngineContext& ctx)
{
    std::vector<PathPoint> path;
    auto outcome = readKPath(config.kPathFile, path);
    if (!outcome.ok())
        return outcome;

    std::array<core::Vec3, 3> latticeBohr{};
    for (int a = 0; a < 3; ++a)
        latticeBohr[static_cast<std::size_t>(a)] =
            ctx.structure.cell().vectors()[static_cast<std::size_t>(a)]
            * dft::kBohrPerAngstrom;
    const auto reciprocal = reciprocalVectors(latticeBohr);

    const std::vector<double> convergedShift = convergedShiftFor(ctx);

    std::vector<double> x(path.size(), 0.0);
    for (std::size_t i = 1; i < path.size(); ++i) {
        core::Vec3 dk{};
        for (int a = 0; a < 3; ++a) {
            const double df = path[i].fractional[static_cast<std::size_t>(a)]
                - path[i - 1].fractional[static_cast<std::size_t>(a)];
            dk += reciprocal[static_cast<std::size_t>(a)] * df;
        }
        x[i] = x[i - 1] + dk.norm();
    }

    const int dimension = ctx.hamiltonian.dimension();
    std::vector<std::vector<double>> energiesPerK(path.size());
    int lowestKept = dimension, highestKept = 0;
    for (std::size_t i = 0; i < path.size(); ++i) {
        progress(static_cast<int>(i) + 1, static_cast<int>(path.size()));
        std::vector<std::complex<double>> h, s;
        ctx.hamiltonian.blochMatrices(path[i].fractional, h, s, convergedShift);
        std::vector<double> eigenvalues;
        std::vector<std::complex<double>> eigenvectors;
        const auto outcome = dft::linalg::solveGeneralizedHermitian(
            h, s, static_cast<std::size_t>(dimension), eigenvalues, eigenvectors);
        if (!outcome.ok())
            return outcome;
        energiesPerK[i] = eigenvalues;
    }

    // Band-selection window around the Fermi level, matching
    // TwoDBandsScriptGenerator's own convention: rather than picking by a
    // single k-point (unreliable across a band crossing), select by GLOBAL
    // band index — the highest band that stays fully below E_F everywhere
    // on the path, minus bandsBelow, to the lowest band that stays fully
    // above it everywhere, plus bandsAbove.
    const double fermi = ctx.scf.fermiEnergyHartree;
    int highestBelow = -1, lowestAbove = dimension;
    for (int b = 0; b < dimension; ++b) {
        double maxAtB = -1.0e300, minAtB = 1.0e300;
        for (const auto& e : energiesPerK) {
            maxAtB = std::max(maxAtB, e[static_cast<std::size_t>(b)]);
            minAtB = std::min(minAtB, e[static_cast<std::size_t>(b)]);
        }
        if (maxAtB <= fermi) highestBelow = b;
        if (minAtB >= fermi && lowestAbove == dimension) lowestAbove = b;
    }
    lowestKept = std::max(0, highestBelow - config.bandsBelow + 1);
    highestKept = std::min(dimension - 1, lowestAbove + config.bandsAbove - 1);
    if (highestBelow < 0) lowestKept = 0;
    if (lowestAbove >= dimension) highestKept = dimension - 1;

    std::ofstream out(config.outputPath);
    if (!out)
        return dft::Outcome::invalid("cannot write " + config.outputPath);
    out << std::setprecision(17);
    out << "{\n  \"x\": [";
    for (std::size_t i = 0; i < x.size(); ++i)
        out << (i == 0 ? "" : ", ") << x[i];
    out << "],\n  \"special_x\": [";
    bool firstSpecial = true;
    for (std::size_t i = 0; i < path.size(); ++i) {
        if (path[i].label.empty()) continue;
        out << (firstSpecial ? "" : ", ") << x[i];
        firstSpecial = false;
    }
    out << "],\n  \"special_labels\": [";
    firstSpecial = true;
    for (const auto& p : path) {
        if (p.label.empty()) continue;
        out << (firstSpecial ? "" : ", ") << "\"" << p.label << "\"";
        firstSpecial = false;
    }
    out << "],\n  \"efermi\": " << fermi * dft::kHartreeToEv << ",\n"
        << "  \"energies\": [[";
    for (std::size_t i = 0; i < path.size(); ++i) {
        out << (i == 0 ? "" : ", ") << "[";
        for (int b = lowestKept; b <= highestKept; ++b)
            out << (b == lowestKept ? "" : ", ")
                << energiesPerK[i][static_cast<std::size_t>(b)] * dft::kHartreeToEv;
        out << "]";
    }
    out << "]]\n}\n";
    std::printf("CALANGO_RESULT bands=%s\n", config.outputPath.c_str());
    std::fflush(stdout);
    return dft::Outcome::success();
}

dft::Outcome runUnfolding(const DftbTaskConfig& config, EngineContext& ctx)
{
    std::vector<PathPoint> path; // PRIMITIVE fractional k-points
    auto outcome = readKPath(config.kPathFile, path);
    if (!outcome.ok())
        return outcome;

    core::Structure primitive;
    outcome = loadExtxyzStructure(config.primitiveStructurePath, primitive);
    if (!outcome.ok())
        return outcome;

    DftbUnfoldingMap map;
    double residual = 0.0;
    outcome = DftbUnfoldingMap::build(ctx.structure, primitive, map, &residual);
    if (!outcome.ok())
        return outcome;
    info("unfolding: " + std::to_string(map.imageCount)
         + " primitive images, commensurability residual "
         + std::to_string(residual));
    if (residual > 1.0e-2)
        info("WARNING: residual is large — the supercell may not actually "
             "be an integer multiple of the given primitive cell");

    std::array<core::Vec3, 3> primitiveLatticeBohr{};
    for (int a = 0; a < 3; ++a)
        primitiveLatticeBohr[static_cast<std::size_t>(a)] =
            primitive.cell().vectors()[static_cast<std::size_t>(a)]
            * dft::kBohrPerAngstrom;
    const auto primitiveReciprocal = reciprocalVectors(primitiveLatticeBohr);

    const std::vector<double> convergedShift = convergedShiftFor(ctx);
    const int dimension = ctx.hamiltonian.dimension();

    // Compute every column FIRST (need the full x[] before writing
    // special_x/special_labels), then write the file in one pass.
    std::vector<double> x(path.size(), 0.0);
    struct Column {
        double pathCoordinate = 0.0;
        std::vector<double> energiesEv;
        std::vector<double> weights;
    };
    std::vector<Column> columns(path.size());
    for (std::size_t i = 1; i < path.size(); ++i) {
        core::Vec3 dk{};
        for (int a = 0; a < 3; ++a) {
            const double df = path[i].fractional[static_cast<std::size_t>(a)]
                - path[i - 1].fractional[static_cast<std::size_t>(a)];
            dk += primitiveReciprocal[static_cast<std::size_t>(a)] * df;
        }
        x[i] = x[i - 1] + dk.norm();
    }
    for (std::size_t i = 0; i < path.size(); ++i) {
        progress(static_cast<int>(i) + 1, static_cast<int>(path.size()));
        const core::Vec3 folded = core::foldToSupercell(
            {path[i].fractional[0], path[i].fractional[1], path[i].fractional[2]},
            map.matrix);
        std::vector<std::complex<double>> h, s;
        ctx.hamiltonian.blochMatrices({folded.x, folded.y, folded.z}, h, s,
                                      convergedShift);
        std::vector<double> eigenvalues;
        std::vector<std::complex<double>> eigenvectors;
        const auto eigenOutcome = dft::linalg::solveGeneralizedHermitian(
            h, s, static_cast<std::size_t>(dimension), eigenvalues, eigenvectors);
        if (!eigenOutcome.ok())
            return eigenOutcome;
        const auto weights = dftbUnfoldingWeights(eigenvectors, s, ctx.basis,
                                                   map, path[i].fractional);
        columns[i].pathCoordinate = x[i];
        for (double e : eigenvalues)
            columns[i].energiesEv.push_back(e * dft::kHartreeToEv);
        columns[i].weights = weights;
    }

    std::ofstream out(config.outputPath);
    if (!out)
        return dft::Outcome::invalid("cannot write " + config.outputPath);
    out << std::setprecision(17);
    out << "{\n"
        << "  \"efermi\": " << ctx.scf.fermiEnergyHartree * dft::kHartreeToEv
        << ",\n"
        << "  \"special_x\": [";
    bool firstSpecial = true;
    for (std::size_t i = 0; i < path.size(); ++i) {
        if (path[i].label.empty()) continue;
        out << (firstSpecial ? "" : ", ") << x[i];
        firstSpecial = false;
    }
    out << "],\n  \"special_labels\": [";
    firstSpecial = true;
    for (const auto& p : path) {
        if (p.label.empty()) continue;
        out << (firstSpecial ? "" : ", ") << "\"" << p.label << "\"";
        firstSpecial = false;
    }
    out << "],\n"
        << "  \"energy_min\": " << config.unfoldingEnergyMinEv << ",\n"
        << "  \"energy_max\": " << config.unfoldingEnergyMaxEv << ",\n"
        << "  \"energy_bins\": " << config.unfoldingEnergyBins << ",\n"
        << "  \"sigma\": " << config.unfoldingSigmaEv << ",\n"
        << "  \"weight_threshold\": " << config.unfoldingWeightThreshold << ",\n"
        << "  \"columns\": [";
    for (std::size_t i = 0; i < columns.size(); ++i) {
        out << (i == 0 ? "" : ", ") << "{\"path_coordinate\": "
            << columns[i].pathCoordinate << ", \"energies\": [";
        for (std::size_t b = 0; b < columns[i].energiesEv.size(); ++b)
            out << (b == 0 ? "" : ", ") << columns[i].energiesEv[b];
        out << "], \"weights\": [";
        for (std::size_t b = 0; b < columns[i].weights.size(); ++b)
            out << (b == 0 ? "" : ", ") << columns[i].weights[b];
        out << "]}";
    }
    out << "]\n}\n";
    std::printf("CALANGO_RESULT effective_bands=%s\n", config.outputPath.c_str());
    std::fflush(stdout);
    return dft::Outcome::success();
}

void writeJsonArray(std::ofstream& out, const std::vector<double>& values)
{
    out << "[";
    for (std::size_t i = 0; i < values.size(); ++i)
        out << (i == 0 ? "" : ", ") << values[i];
    out << "]";
}

dft::Outcome runOptics(const DftbTaskConfig& config, EngineContext& ctx)
{
    calango::dftb::DftbOpticsOptions options;
    options.frequenciesEv.resize(
        static_cast<std::size_t>(std::max(1, config.opticsSteps)) + 1);
    for (std::size_t i = 0; i < options.frequenciesEv.size(); ++i)
        options.frequenciesEv[i] = config.opticsOmegaMaxEv * static_cast<double>(i)
            / static_cast<double>(options.frequenciesEv.size() - 1);
    options.broadeningEv = config.opticsBroadeningEv;
    options.direction = config.opticsDirection;
    options.vacuumThicknessAngstrom = config.opticsVacuumThicknessAngstrom;

    std::array<core::Vec3, 3> latticeBohr{};
    for (int a = 0; a < 3; ++a)
        latticeBohr[static_cast<std::size_t>(a)] =
            ctx.structure.cell().vectors()[static_cast<std::size_t>(a)]
            * dft::kBohrPerAngstrom;
    const double volumeBohr3 =
        std::fabs(latticeBohr[0].dot(latticeBohr[1].cross(latticeBohr[2])));
    if (volumeBohr3 < 1.0e-6)
        return dft::Outcome::invalid(
            "cell volume is degenerate — optics needs a genuine 3-vector "
            "cell (vacuum axes included, as every other Calango optics "
            "generator already requires)");

    const std::vector<double> convergedShift = convergedShiftFor(ctx);

    progress(1, 2);
    calango::dftb::DftbOpticsResult result;
    const auto outcome = calango::dftb::computeDftbOptics(
        ctx.scf, ctx.hamiltonian, latticeBohr, volumeBohr3, convergedShift,
        options, result);
    if (!outcome.ok())
        return outcome;
    progress(2, 2);

    const char* key = config.opticsDirection == 0 ? "xx"
        : config.opticsDirection == 1 ? "yy" : "zz";

    std::ofstream out(config.outputPath);
    if (!out)
        return dft::Outcome::invalid("cannot write " + config.outputPath);
    out << std::setprecision(17);
    out << "{\n  \"energy_eV\": ";
    writeJsonArray(out, result.frequenciesEv);
    out << ",\n  \"engine\": \"Calango DFTB\",\n"
        << "  \"" << key << "\": {\n"
        << "    \"eps1\": "; writeJsonArray(out, result.eps1);
    out << ",\n    \"eps2\": "; writeJsonArray(out, result.eps2);
    out << ",\n    \"absorption\": "; writeJsonArray(out, result.absorptionInverseCm);
    out << ",\n    \"reflectivity\": "; writeJsonArray(out, result.reflectivity);
    out << ",\n    \"n\": "; writeJsonArray(out, result.n);
    out << ",\n    \"k\": "; writeJsonArray(out, result.k);
    out << ",\n    \"loss\": "; writeJsonArray(out, result.loss);
    out << "\n  },\n"
        << "  \"eps_" << key << "\": {\n"
        << "    \"eps1\": "; writeJsonArray(out, result.eps1);
    out << ",\n    \"eps2\": "; writeJsonArray(out, result.eps2);
    out << "\n  }";
    if (config.opticsVacuumThicknessAngstrom > 0.0) {
        out << ",\n  \"twod_" << key << "\": {\n"
            << "    \"alpha_2D_re_A\": "; writeJsonArray(out, result.alpha2DReAngstrom);
        out << ",\n    \"alpha_2D_im_A\": "; writeJsonArray(out, result.alpha2DImAngstrom);
        out << ",\n    \"absorbance\": "; writeJsonArray(out, result.absorbance);
        out << ",\n    \"sigma_2D_re\": "; writeJsonArray(out, result.sigma2DRe);
        out << ",\n    \"sigma_2D_im\": "; writeJsonArray(out, result.sigma2DIm);
        out << "\n  }";
    }
    out << "\n}\n";
    std::printf("CALANGO_RESULT optics=%s\n", config.outputPath.c_str());
    std::fflush(stdout);
    return dft::Outcome::success();
}

dft::Outcome runPdos(const DftbTaskConfig& config, EngineContext& ctx)
{
    progress(1, 2);
    DftbPdosResult result;
    const auto outcome = computeDftbPdos(
        ctx.scf, ctx.hamiltonian, ctx.basis, ctx.table,
        config.pdosBroadeningHartree, config.pdosBinWidthHartree, result);
    if (!outcome.ok())
        return outcome;
    progress(2, 2);

    std::ofstream out(config.outputPath);
    if (!out)
        return dft::Outcome::invalid("cannot write " + config.outputPath);
    out << std::setprecision(17);
    out << "{\n  \"energies\": [";
    for (std::size_t i = 0; i < result.energiesEv.size(); ++i)
        out << (i == 0 ? "" : ", ") << result.energiesEv[i];
    out << "],\n"
        << "  \"efermi\": " << result.fermiEv << ",\n"
        << "  \"broadened\": true,\n"
        << "  \"integration\": \"sampling\",\n"
        << "  \"bin_width\": " << result.binWidthEv << ",\n"
        << "  \"projections\": {\n";
    bool firstGroup = true;
    for (const auto& [name, values] : result.projections) {
        out << (firstGroup ? "" : ",\n") << "    \"" << name << "\": [";
        for (std::size_t i = 0; i < values.size(); ++i)
            out << (i == 0 ? "" : ", ") << values[i];
        out << "]";
        firstGroup = false;
    }
    out << "\n  }\n}\n";
    std::printf("CALANGO_RESULT pdos=%s\n", config.outputPath.c_str());
    std::fflush(stdout);
    return dft::Outcome::success();
}

} // namespace

dft::Outcome runDftbTask(const DftbTaskConfig& config)
{
    EngineContext ctx;
    auto outcome = prepareAndRunScf(config, ctx);
    if (!outcome.ok())
        return outcome;

    switch (config.task) {
    case DftbTask::SinglePoint:
        return runSinglePoint(config, ctx);
    case DftbTask::Bands:
        return runBands(config, ctx);
    case DftbTask::Pdos:
        return runPdos(config, ctx);
    case DftbTask::Unfolding:
        return runUnfolding(config, ctx);
    case DftbTask::Optics:
        return runOptics(config, ctx);
    }
    return dft::Outcome::notImplemented("unknown task");
}

} // namespace calango::dftb
