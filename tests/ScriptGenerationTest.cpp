// Generated-ASE-script test.
//
// Covers the two things that can silently break a run long after the wizard
// closed: (1) the script must be SELF-CONTAINED — its logging block inlined,
// with no import of any Calango helper module — so it still runs on a cluster
// that has ASE and nothing else, and (2) the calculator blocks must spell out
// the parameters the wizard collected (MACE precision / weights file / device;
// GPAW mode, xc, eigensolver, mixer, convergence).
//
// GUI-free and Python-free. With `--dump <dir>` it writes each generated
// script to disk instead of asserting, so a shell step can byte-compile them
// against a real ASE install (see the accompanying check in the repo docs).

#include "core/AseScriptGenerator.hpp"
#include "core/GrapheneOxideMdmcScriptGenerator.hpp"
#include "core/BornChargesScriptGenerator.hpp"
#include "core/PiezoelectricScriptGenerator.hpp"
#include "core/CalphadScriptGenerator.hpp"
#include "core/CddScriptGenerator.hpp"
#include "core/ClusterExpansionScriptGenerator.hpp"
#include "core/UnfoldingScriptGenerator.hpp"
#include "core/XasScriptGenerator.hpp"
#include "core/ElectronicScriptGenerator.hpp"
#include "core/ElectronPhononScriptGenerator.hpp"
#include "core/GwScriptGenerator.hpp"
#include "core/KpointsConvergenceScriptGenerator.hpp"
#include "core/NonlinearOpticsScriptGenerator.hpp"
#include "core/OpticsScriptGenerator.hpp"
#include "core/PhononScriptGenerator.hpp"
#include "core/ThermodynamicIntegrationScriptGenerator.hpp"
#include "core/RamanIrScriptGenerator.hpp"
#include "core/RandomNoiseScriptGenerator.hpp"
#include "core/Defect2dScriptGenerator.hpp"
#include "core/DefectScriptGenerator.hpp"
#include "core/FermiSurfaceScriptGenerator.hpp"
#include "core/TopologyScriptGenerator.hpp"
#include "core/TwoDBandsScriptGenerator.hpp"
#include "core/WannierScriptGenerator.hpp"
#include "core/WorkfunctionScriptGenerator.hpp"

#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <fstream>
#include <string>

#include "core/LocaleSafeNumber.hpp"

#include <algorithm>
#include <string_view>

using namespace calango::core;

namespace {

int failures = 0;

void check(bool condition, const std::string& what)
{
    std::printf("  %s %s\n", condition ? "ok  " : "FAIL", what.c_str());
    if (!condition)
        ++failures;
}

bool contains(const std::string& haystack, const std::string& needle)
{
    return haystack.find(needle) != std::string::npos;
}

void checkContains(const std::string& script, const std::string& needle,
                   const std::string& what)
{
    check(contains(script, needle), what + "  [" + needle + "]");
}

void checkNotContains(const std::string& script, const std::string& needle,
                      const std::string& what)
{
    check(!contains(script, needle), what + "  [no " + needle + "]");
}

CalculatorConfig maceConfig()
{
    CalculatorConfig c;
    c.calculator = CalculatorKind::Mace;
    c.task = TaskKind::SinglePoint;
    return c;
}

CalculatorConfig gpawConfig()
{
    CalculatorConfig c;
    c.calculator = CalculatorKind::Gpaw;
    c.task = TaskKind::SinglePoint;
    return c;
}

/// A relaxation with all three constraint shapes at once: whole atoms frozen,
/// a partial direction mask, and a two-sided region rule.
CalculatorConfig constrainedRelaxation()
{
    CalculatorConfig c = gpawConfig();
    c.task = TaskKind::GeometryOptimization;

    GeometryConstraint fixed;               // atoms 0,1,2 frozen entirely
    fixed.indices = {0, 1, 2};
    GeometryConstraint inPlane;             // atom 7 free only in x/y
    inPlane.indices = {7};
    inPlane.fix[0] = false;
    inPlane.fix[1] = false;
    inPlane.fix[2] = true;
    GeometryConstraint slab;                // 5 < z < 10, frozen entirely
    slab.selection = GeometryConstraint::Selection::Region;
    slab.axis = 2;
    slab.hasMin = true;
    slab.minValue = 5.0;
    slab.hasMax = true;
    slab.maxValue = 10.0;

    c.constraints = {fixed, inPlane, slab};
    return c;
}

} // namespace

int main(int argc, char** argv)
{
    // --dump <dir>: write the scripts out for external `python -m py_compile`.
    if (argc >= 3 && std::string(argv[1]) == "--dump") {
        const std::string dir = argv[2];
        const auto dump = [&dir](const std::string& name,
                                 const CalculatorConfig& config) {
            std::ofstream out(dir + "/" + name);
            out << AseScriptGenerator::generate(config, "structure.extxyz");
        };
        // Generators that emit a whole script of their own rather than an
        // AseScriptGenerator task go through this instead.
        const auto dumpText = [&dir](const std::string& name,
                                     const std::string& script) {
            std::ofstream out(dir + "/" + name);
            out << script;
        };
        CalculatorConfig mace = maceConfig();
        mace.macePrecision = MacePrecision::Float32;
        mace.maceDevice = "mps";
        // Graphene oxide MDMC: the longest generated script in the project
        // and the one with the most Python of its own, so the lint matters
        // most here. Emitted for both ensembles - the NPT branch adds an
        // import and a different integrator, so one dump would leave half of
        // it unchecked.
        {
            GrapheneOxideMdmcConfig mdmc;
            mdmc.calculator.calculator = CalculatorKind::Mace;
            dumpText("graphene_oxide_mdmc_nvt.py",
                 GrapheneOxideMdmcScriptGenerator::generate(mdmc));
            mdmc.constantPressure = true;
            mdmc.pressureGpa = 0.1;
            dumpText("graphene_oxide_mdmc_npt.py",
                 GrapheneOxideMdmcScriptGenerator::generate(mdmc));
        }

        // Electron-phonon coupling: the three-stage gpaw.elph workflow. Worth
        // byte-compiling because its body is generated Python doing real array
        // work — the k+q map, the ordering guard and the manifest — rather
        // than keyword plumbing. (The alpha^2F sums it used to contain now
        // live in C++, where ElectronPhononAnalysisTest pins them against a
        // closed form.)
        {
            ElectronPhononConfig epc;
            epc.calculator = gpawConfig();
            dumpText("electron_phonon.py",
                     generateElectronPhononScript(epc));
            // A second, deliberately small variant the aluminium benchmark
            // RUNS end to end. Its meshes are the smallest that still produce
            // a meaningful lambda, chosen so the integration test is minutes
            // rather than hours — see au/al benchmark notes.
            ElectronPhononConfig small = epc;
            small.basis = "sz(dzp)";
            small.kGrid[0] = small.kGrid[1] = small.kGrid[2] = 6;
            small.qGrid[0] = small.qGrid[1] = small.qGrid[2] = 2;
            small.supercell[0] = small.supercell[1] = small.supercell[2] = 2;
            dumpText("electron_phonon_small.py",
                     generateElectronPhononScript(small));
        }

        // Cluster expansion WITH a design matrix — the path the ECI fitter
        // needs — and without, which must still be valid Python.
        {
            ClusterExpansionRunConfig ce;
            ce.calculator = gpawConfig();
            ce.correlations = {{1.0, 0.5, -0.25}, {1.0, -0.5, 0.25}};
            ce.orbitLabels = {"empty", "pair r=2.55 m=12", "triplet r=2.55"};
            dumpText("cluster_expansion_design.py",
                     ClusterExpansionScriptGenerator::generate(ce));
            ClusterExpansionRunConfig bare = ce;
            bare.correlations.clear();
            bare.orbitLabels.clear();
            dumpText("cluster_expansion_nodesign.py",
                     ClusterExpansionScriptGenerator::generate(bare));
        }

        // K-point convergence, with and without the plasma-frequency target.
        // Both branches, because the ω_p option adds a module-level helper
        // and two blocks inside the sweep loop — the lint is what catches a
        // NameError in generated Python that no C++ test can see.
        {
            KpointsConvergenceRunConfig kpts;
            kpts.calculator = gpawConfig();
            kpts.meshes = {{{2, 2, 2}, 2}, {{4, 4, 4}, 4}, {{6, 6, 6}, 6}};
            dumpText("kpoints_convergence.py",
                     KpointsConvergenceScriptGenerator::generate(kpts));
            kpts.plasmaFrequency = true;
            dumpText("kpoints_convergence_plasma.py",
                     KpointsConvergenceScriptGenerator::generate(kpts));
        }

        // Thermodynamic integration. Four dumps, because the branches that
        // differ are the ones the lint can actually catch: each reference
        // system emits a different calculator class and a different set of
        // module-level constants, the hysteresis option adds a second sweep
        // over the same loop, and the split-job form changes WINDOW_INDICES
        // and withdraws the backward sweep. A `_name` read in the sampling
        // loop whose definition was lost in an edit is valid Python that dies
        // at run time, halfway through somebody's free energy.
        {
            TiRunConfig ti;
            ti.calculator = maceConfig();
            ti.calculator.task = TaskKind::MolecularDynamics;
            ti.resultsDir = "/tmp/calango_ti";
            dumpText("ti_ideal_gas.py",
                     ThermodynamicIntegrationScriptGenerator::generate(ti));

            TiRunConfig einstein = ti;
            einstein.calculator = gpawConfig();
            einstein.calculator.task = TaskKind::MolecularDynamics;
            einstein.reference = TiReference::EinsteinCrystal;
            einstein.schedule = TiLambdaSchedule::Uniform;
            einstein.quadrature = TiQuadrature::Simpson;
            einstein.hysteresis = true;
            einstein.windows = 9;
            dumpText("ti_einstein_hysteresis.py",
                     ThermodynamicIntegrationScriptGenerator::generate(
                         einstein));

            TiRunConfig lj = ti;
            lj.reference = TiReference::LennardJonesFluid;
            lj.schedule = TiLambdaSchedule::PowerLaw;
            lj.calculator.ensemble = MdEnsemble::BerendsenNPT;
            dumpText("ti_lennard_jones_npt.py",
                     ThermodynamicIntegrationScriptGenerator::generate(lj));

            TiRunConfig slice = ti;
            slice.windowIndices = {3, 4, 5};
            slice.hysteresis = true; // must be withdrawn: this job owns a slice
            dumpText("ti_window_slice.py",
                     ThermodynamicIntegrationScriptGenerator::generate(slice));
        }

        dump("mace_single_point.py", mace);

        CalculatorConfig maceCustom = maceConfig();
        maceCustom.maceSource = MaceModelSource::CustomFile;
        maceCustom.maceModelPath = "/models/mace-off23-small.model";
        maceCustom.task = TaskKind::MolecularDynamics;
        dump("mace_md.py", maceCustom);

        for (const auto [name, mode] :
             {std::pair{"gpaw_pw.py", GpawMode::PlaneWave},
              std::pair{"gpaw_fd.py", GpawMode::FiniteDifference},
              std::pair{"gpaw_lcao.py", GpawMode::Lcao}}) {
            CalculatorConfig gpaw = gpawConfig();
            gpaw.gpawMode = mode;
            gpaw.task = TaskKind::GeometryOptimization;
            dump(name, gpaw);
        }

        // xTB, one script per method. Dumped so the integration test can RUN
        // them: xTB is fast enough (a water molecule is milliseconds) that the
        // generated script can be executed for real rather than only compiled,
        // which is the only way to catch a parameter the ASE calculator
        // silently ignores or rejects.
        for (const auto* method : {"GFN2-xTB", "GFN1-xTB", "GFN-FF"}) {
            CalculatorConfig xtb;
            xtb.calculator = CalculatorKind::Xtb;
            xtb.task = TaskKind::SinglePoint;
            xtb.xtbMethod = method;
            std::string name(method);
            for (char& ch : name)
                ch = ch == '-' ? '_' : static_cast<char>(std::tolower(ch));
            dump("xtb_" + name + ".py", xtb);
        }

        // DFT+U and dispersion ride on top of a normal GPAW block, so they
        // are dumped together — the setups dict and the DFTD4 wrap are both
        // appended to code that already has to stay syntactically valid.
        CalculatorConfig hubbard = gpawConfig();
        hubbard.task = TaskKind::GeometryOptimization;
        hubbard.useHubbardU = true;
        hubbard.hubbardU = {{"Fe", "d", 3.5, false}, {"O", "p", 2.0, true}};
        hubbard.dispersionD4 = true;
        dump("gpaw_hubbard_d4.py", hubbard);

        // Constraints emit a list comprehension inside a generated script that
        // already mixes literal lists and f-strings, so its indentation is
        // worth byte-compiling rather than eyeballing. Once free-cell and once
        // variable-cell: the constraint block sits between max_steps and the
        // cell filter, and only the second layout exercises that neighbour.
        dump("gpaw_constraints.py", constrainedRelaxation());
        CalculatorConfig constrainedCell = constrainedRelaxation();
        constrainedCell.relaxCell = true;
        constrainedCell.cellCustomMask = true;
        dump("gpaw_constraints_cell.py", constrainedCell);

        CalculatorConfig emt;
        emt.task = TaskKind::MolecularDynamics;
        emt.ensemble = MdEnsemble::BerendsenNPT;
        dump("emt_npt.py", emt);

        // Every MLIP block, so a syntax error in one cannot hide behind the
        // others (each emits a different import + constructor shape).
        for (const auto [name, kind] :
             {std::pair{"mlip_deepmd.py", CalculatorKind::DeepMd},
              std::pair{"mlip_nequip.py", CalculatorKind::NequIp},
              std::pair{"mlip_allegro.py", CalculatorKind::Allegro},
              std::pair{"mlip_chgnet.py", CalculatorKind::ChgNet},
              std::pair{"mlip_mattersim.py", CalculatorKind::MatterSim},
              std::pair{"mlip_fairchem.py", CalculatorKind::FairChem}}) {
            CalculatorConfig c;
            c.calculator = kind;
            c.task = TaskKind::GeometryOptimization;
            c.mlipDevice = MlipDevice::Cuda;
            c.deepmdModelPath = "/models/frozen.pb";
            c.nequipModelPath = "/models/deployed.pth";
            c.fairChemCheckpointPath = "/models/eq2.pt";
            c.matterSimThermal = true;
            dump(name, c);
        }

        // GPAW density exports: all six fields at once. The block defines
        // nested helpers and lambdas inside an f-string-bearing script, which
        // is exactly the shape a hand-check misses.
        {
            CalculatorConfig densities = gpawConfig();
            densities.gpawExportDensity = true;
            densities.gpawDensityExports = {true, true, true, true, true, true};
            densities.spinPolarized = true;
            densities.spinMode = SpinMode::Collinear;
            std::ofstream out(dir + "/gpaw_densities.py");
            out << AseScriptGenerator::generate(densities, "structure.extxyz");
        }

        // VASP: the POTCAR shim, the INCAR tags and the free-form extras all
        // land in one block with a nested loop and an f-string, so it is worth
        // byte-compiling rather than eyeballing.
        {
            CalculatorConfig vasp;
            vasp.calculator = CalculatorKind::Vasp;
            vasp.task = TaskKind::SinglePoint;
            vasp.vaspPotcarPath = "/opt/vasp/POTCARs";
            vasp.spinPolarized = true;
            vasp.spinMode = SpinMode::Collinear;
            vasp.vaspLaechg = true;
            vasp.vaspLorbit = true;
            vasp.vaspNcore = 4;
            vasp.vaspKpar = 2;
            vasp.vaspExtraIncar = "LDAU = .TRUE.\nLDAUU = 4.0 0.0";
            dump("vasp_single_point.py", vasp);

            CalculatorConfig relax = vasp;
            relax.task = TaskKind::GeometryOptimization;
            relax.relaxCell = true;
            relax.vaspExtraIncar.clear();
            relax.vaspPotcarPath.clear(); // the no-POTCAR-configured branch
            dump("vasp_relax.py", relax);

            // The other relaxation driver: VASP takes the ionic steps and no
            // ASE optimizer exists. A whole separate task body, so it needs
            // byte-compiling like the rest.
            CalculatorConfig internalRelax = relax;
            internalRelax.vaspRelaxDriver = VaspRelaxDriver::Vasp;
            internalRelax.vaspPotcarPath = "/opt/vasp/POTCARs";
            dump("vasp_relax_internal.py", internalRelax);
        }

        // FHI-aims and SIESTA single-point, bulk-Si-shaped settings: k-grid,
        // xc, spin all set the way the wizard would set them for a real
        // periodic run, so the dump can be dry-run against a real ASE
        // install (task 2/3 verification).
        {
            CalculatorConfig aims;
            aims.calculator = CalculatorKind::FhiAims;
            aims.task = TaskKind::SinglePoint;
            aims.kpts[0] = aims.kpts[1] = aims.kpts[2] = 4;
            aims.aimsXc = "pbe";
            aims.aimsSpeciesDir = "/opt/aims/species_defaults";
            aims.aimsSpeciesTier = "light";
            dump("aims_single_point.py", aims);

            CalculatorConfig aimsSpin = aims;
            aimsSpin.spinPolarized = true;
            aimsSpin.spinMode = SpinMode::Collinear;
            aimsSpin.initialMagMoment = 2.0;
            dump("aims_single_point_spin.py", aimsSpin);

            CalculatorConfig siesta;
            siesta.calculator = CalculatorKind::Siesta;
            siesta.task = TaskKind::SinglePoint;
            siesta.kpts[0] = siesta.kpts[1] = siesta.kpts[2] = 4;
            siesta.siestaXc = "PBE";
            siesta.siestaBasisSize = "DZP";
            siesta.siestaPseudoDir = "/opt/psml";
            dump("siesta_single_point.py", siesta);

            CalculatorConfig siestaSpin = siesta;
            siestaSpin.spinPolarized = true;
            siestaSpin.spinMode = SpinMode::Collinear;
            dump("siesta_single_point_spin.py", siestaSpin);
        }

        // VASP band structure, with and without a reused CHGCAR: the baseline
        // branch is a whole separate block with a file copy and a guard in it.
        {
            ElectronicConfig bands;
            bands.backend = ElectronicBackend::Vasp;
            bands.kpath = "GXWKG";
            std::ofstream fresh(dir + "/vasp_bands_scf.py");
            fresh << generateElectronicScript(bands);

            ElectronicConfig nscf = bands;
            nscf.baselineDensityPath = "/jobs/proc_1/CHGCAR";
            std::ofstream reused(dir + "/vasp_bands_nscf.py");
            reused << generateElectronicScript(nscf);
        }

        // Band unfolding: the commensurability guard is now a relative test
        // with a configurable bound, and it has to still be valid Python.
        {
            UnfoldingConfig unfold;
            unfold.kpath = "GXWKG";
            std::ofstream out(dir + "/unfolding.py");
            out << generateUnfoldingScript(unfold);
        }

        // XAS: three stages in one script, with a nested Generator call and
        // an optional delta-Kohn-Sham block that doubles its length.
        {
            XasRunConfig xas;
            xas.element = "O";
            xas.calculator.calculator = CalculatorKind::Gpaw;
            std::ofstream out(dir + "/xas_half.py");
            out << XasScriptGenerator::generate(xas, "structure.extxyz");

            XasRunConfig dks = xas;
            dks.coreHole = XasCoreHole::Full;
            dks.coreLevel = XasCoreLevel::L23;
            dks.computeDks = true;
            dks.linearBroadening = false;
            std::ofstream dksOut(dir + "/xas_dks.py");
            dksOut << XasScriptGenerator::generate(dks, "structure.extxyz");
        }

        // Charge density difference: the fragment loop rebuilds a calculator
        // from a **params dict inside a function, two levels of nesting under
        // an f-string-bearing script.
        {
            CddRunConfig cdd;
            cdd.baselineDir = dir;
            cdd.subsystemB = {1};
            std::ofstream out(dir + "/cdd_all_electron.py");
            out << CddScriptGenerator::generate(cdd);

            CddRunConfig pseudo = cdd;
            pseudo.allElectron = false;
            pseudo.subsystemB = {2, 3, 5};
            std::ofstream pseudoOut(dir + "/cdd_pseudo.py");
            pseudoOut << CddScriptGenerator::generate(pseudo);
        }

        // Random-noise ensemble sweep: the calculator block is re-indented
        // into a function body and the per-member loop nests two more levels
        // under it, so this is exactly the shape where an indentation slip
        // parses as valid Python that does the wrong thing.
        {
            RandomNoiseRunConfig noise;
            noise.calculator = gpawConfig();
            noise.calculator.task = TaskKind::SinglePoint;
            noise.computeForces = true;
            noise.computeStress = true;
            std::ofstream out(dir + "/random_noise.py");
            out << RandomNoiseScriptGenerator::generate(noise);

            // The failure branch is a `raise` rather than a recovery block, so
            // both variants are compiled.
            RandomNoiseRunConfig strict = noise;
            strict.continueOnFailure = false;
            strict.computeForces = false;
            strict.computeStress = false;
            std::ofstream strictOut(dir + "/random_noise_strict.py");
            strictOut << RandomNoiseScriptGenerator::generate(strict);
        }

        // LAMMPS: both interfaces, since they emit structurally different
        // blocks (a command LIST for the library, a parameter DICT for the
        // executable), plus the units guard, which is a bare `raise` that must
        // still parse.
        {
            CalculatorConfig lammps;
            lammps.calculator = CalculatorKind::Lammps;
            lammps.task = TaskKind::SinglePoint;
            lammps.lammpsPairStyle = "eam/alloy";
            lammps.lammpsPairCoeff = {"* * Cu_u3.eam.alloy Cu"};
            // Inside the dump directory, so the accompanying Python check can
            // create the file and exercise the found branch of the generated
            // guard as well as the missing one.
            lammps.lammpsPotentialFiles = {dir + "/Cu_u3.eam.alloy"};
            lammps.lammpsExtraCommands = {"neighbor 2.0 bin"};
            dump("lammps_lib.py", lammps);

            CalculatorConfig run = lammps;
            run.lammpsInterface = LammpsInterface::Run;
            run.lammpsCommand = "/usr/bin/lmp_serial";
            run.task = TaskKind::MolecularDynamics;
            dump("lammps_run.py", run);

            CalculatorConfig multi = lammps;
            multi.lammpsPairStyle = "lj/cut 10.0";
            multi.lammpsPairCoeff = {"1 1 0.0103 3.4", "1 2 0.0140 3.1",
                                     "2 2 0.0180 2.9"};
            multi.lammpsPotentialFiles.clear();
            multi.task = TaskKind::GeometryOptimization;
            dump("lammps_multi.py", multi);

            CalculatorConfig realUnits = lammps;
            realUnits.lammpsUnits = "real";
            dump("lammps_bad_units.py", realUnits);
        }

        // xTB / DFTB+ / GROMACS: one dump per structurally distinct branch.
        // GFN-FF drops the SCC kwargs; non-SCC DFTB and the Fermi filling
        // change which HSD keywords are emitted; GROMACS has a single-point
        // pipeline plus an outright refusal that must still parse.
        {
            CalculatorConfig xtb;
            xtb.calculator = CalculatorKind::Xtb;
            xtb.task = TaskKind::GeometryOptimization;
            dump("xtb_gfn2.py", xtb);
            CalculatorConfig gfnff = xtb;
            gfnff.xtbMethod = "GFN-FF";
            dump("xtb_gfnff.py", gfnff);

            CalculatorConfig dftb;
            dftb.calculator = CalculatorKind::DftbPlus;
            dftb.task = TaskKind::SinglePoint;
            dftb.dftbSlakoDir = "/opt/slako/mio-1-1";
            dftb.dftbFillingTemperatureK = 300.0;
            dump("dftb_scc_fermi.py", dftb);
            CalculatorConfig nonScc = dftb;
            nonScc.dftbScc = false;
            nonScc.dftbFillingTemperatureK = 0.0;
            nonScc.dftbSlakoDir.clear(); // the EDIT-ME branch
            dump("dftb_nonscc.py", nonScc);

            CalculatorConfig gromacs;
            gromacs.calculator = CalculatorKind::Gromacs;
            gromacs.task = TaskKind::SinglePoint;
            gromacs.gromacsExtraMdp = "rvdw = 1.0\ncoulombtype = PME";
            dump("gromacs_single_point.py", gromacs);
            CalculatorConfig refused = gromacs;
            refused.task = TaskKind::GeometryOptimization;
            dump("gromacs_refused.py", refused);
        }

        // Phonon drivers: the plain 6N path and the symmetry-reduced one, each
        // with and without residual force removal. The symmetry-reduced script
        // nests both drivers in functions, so its indentation is worth
        // byte-compiling rather than eyeballing.
        const auto dumpPhonon = [&dir](const std::string& name,
                                       const PhononConfig& config) {
            std::ofstream out(dir + "/" + name);
            out << PhononScriptGenerator::generate(config, "structure.extxyz");
        };
        for (const bool symmetry : {false, true}) {
            for (const bool residual : {false, true}) {
                PhononConfig p;
                p.symmetryReducedDisplacements = symmetry;
                p.removeResidualForces = residual;
                p.kpath = "GXMG";
                dumpPhonon(std::string("phonon_") + (symmetry ? "sym" : "full")
                               + (residual ? "_residual" : "") + ".py",
                           p);
                PhononConfig molecule = p;
                molecule.periodic = false;
                dumpPhonon(std::string("vib_") + (symmetry ? "sym" : "full")
                               + (residual ? "_residual" : "") + ".py",
                           molecule);
            }
        }
        // LO-TO splitting: the nac_params block and the Gamma re-evaluation
        // are nested two levels deep inside a function, so their indentation
        // needs byte-compiling like the rest.
        {
            PhononConfig loto;
            loto.kpath = "GXMG";
            loto.bornChargesFile = "/tmp/born_charges.json";
            loto.dielectric[0][0] = 2.96;
            loto.dielectric[1][1] = 2.96;
            loto.dielectric[2][2] = 2.96;
            dumpPhonon("phonon_loto.py", loto);
            loto.symmetryReducedDisplacements = true;
            loto.removeResidualForces = true;
            dumpPhonon("phonon_loto_sym.py", loto);
        }

        // Optics: the 3D form and the 2D-sheet variant. Both inherit a
        // baseline ground state and append a post-processing block, so their
        // indentation is worth byte-compiling rather than eyeballing.
        {
            const auto dumpOptics = [&dir](const std::string& name,
                                           const OpticsConfig& config) {
                std::ofstream out(dir + "/" + name);
                out << generateOpticsScript(config);
            };
            OpticsConfig optics;
            optics.baselineDensityPath = "/jobs/proc_1/single_point.gpw";
            dumpOptics("optics_3d.py", optics);
            OpticsConfig sheet = optics;
            sheet.vacuumAxis = 2;
            dumpOptics("optics_2d.py", sheet);
            OpticsConfig tetra = sheet;
            tetra.tetrahedronIntegration = true;
            dumpOptics("optics_2d_tetrahedron.py", tetra);
            OpticsConfig ibz = optics;
            ibz.responseKpts[0] = ibz.responseKpts[1] = ibz.responseKpts[2] = 12;
            ibz.includeIbzPoints = true;
            dumpOptics("optics_ibz.py", ibz);
            // The Drude branches: each emits a different block, and the
            // explicit-tau one computes its rate in generated Python, so all
            // three are worth byte-compiling rather than only the default.
            OpticsConfig tau = optics;
            tau.drudeRateFromBroadening = false;
            tau.drudeRelaxationTimeFs = 9.3;
            dumpOptics("optics_drude_tau.py", tau);
            OpticsConfig noDrude = optics;
            noDrude.intrabandDrude = false;
            dumpOptics("optics_drude_off.py", noDrude);
            // A 2D sheet with BOTH advanced options on at once — the
            // combination a metallic monolayer actually needs, and the one
            // where the vacuum axis, the tetrahedron mesh search and the
            // free-carrier term all have to coexist.
            OpticsConfig sheetTau = tetra;
            sheetTau.drudeRateFromBroadening = false;
            sheetTau.drudeRelaxationTimeFs = 20.0;
            sheetTau.responseKpts[0] = sheetTau.responseKpts[1] = 12;
            dumpOptics("optics_2d_tetrahedron_drude_tau.py", sheetTau);
        }

        // GW: both engines against both frequency treatments. The Yambo path
        // is the longest generated script in the suite (subprocess driver,
        // input patcher, .qp parser) and has the most indentation to get wrong.
        {
            const auto dumpGw = [&dir](const std::string& name,
                                       const GwConfig& config) {
                std::ofstream out(dir + "/" + name);
                out << generateGwScript(config);
            };
            GwConfig gw;
            gw.baselinePath = "/jobs/proc_1/single_point.gpw";
            dumpGw("gw_gpaw_ppa.py", gw);
            gw.frequency = GwFrequencyTreatment::RealAxis;
            dumpGw("gw_gpaw_realaxis.py", gw);
            GwConfig yambo;
            yambo.engine = GwEngine::Yambo;
            yambo.baselinePath = "/jobs/proc_2";
            yambo.yamboCores = 8;
            dumpGw("gw_yambo_ppa.py", yambo);
            yambo.frequency = GwFrequencyTreatment::RealAxis;
            dumpGw("gw_yambo_realaxis.py", yambo);
        }

        // Charged defects in 2D: dumped so the benchmark runs the script the
        // generator actually emits, rather than a paraphrase of it.
        {
            Defect2dConfig defect2d;
            defect2d.calculator.calculator = CalculatorKind::Gpaw;
            defect2d.pristinePath = "/jobs/host/single_point.gpw";
            defect2d.neutralDefectPath = "/jobs/defect/single_point.gpw";
            // +-2 as well as +-1, so the benchmark's q^2 check on the run's
            // own numbers has something to fail on: with +-1 alone the ratio
            // is 1 whatever the generator did.
            defect2d.charges = {-2, -1, 0, 1, 2};
            defect2d.epsilonInPlane = 6.9;
            defect2d.epsilonOutOfPlane = 2.8;
            defect2d.layerThickness = 6.15;
            defect2d.zComponents = 32;
            defect2d.inPlaneCutoff = 6;
            std::ofstream out(dir + "/charged_defects_2d.py");
            out << generateDefect2dScript(defect2d);
        }

        // Electronic structure with spin-orbit coupling: the SOC block rebuilds
        // `bs` from a numpy array, indented into the middle of the GPAW branch.
        {
            const auto dumpBands = [&dir](const std::string& name,
                                          const ElectronicConfig& config) {
                std::ofstream out(dir + "/" + name);
                out << generateElectronicScript(config);
            };
            ElectronicConfig bands;
            bands.backend = ElectronicBackend::Gpaw;
            bands.kpath = "GXWKG";
            bands.baselineDensityPath = "/jobs/proc_1/single_point.gpw";
            bands.spinOrbit = true;
            dumpBands("bands_soc.py", bands);
            ElectronicConfig inlineScf = bands;
            inlineScf.baselineDensityPath.clear();
            dumpBands("bands_soc_inline_scf.py", inlineScf);

            // Band symmetry + fatbands. Both append large blocks of nested
            // Python — the symmetry one defines two module-level helper
            // functions with comprehensions and a `for/else` inside them, and
            // the fatband one nests three loops under a try. That is exactly
            // the shape where an indentation slip parses as valid Python that
            // does the wrong thing, so both variants are byte-compiled.
            ElectronicConfig symmetry = bands;
            symmetry.spinOrbit = false;
            symmetry.bandSymmetry = true;
            symmetry.fatbands = true;
            dumpBands("bands_symmetry_fatbands.py", symmetry);

            // Tetrahedron DOS. The branch it adds is a try nested inside a
            // double loop inside an else inside an if — the exact shape where
            // a stray indent parses as valid Python that silently does the
            // wrong thing, so it is byte-compiled like the rest. (What the
            // script SAYS is asserted in the normal pass; this dump is about
            // whether Python will accept it at all.)
            ElectronicConfig tetra = bands;
            tetra.spinOrbit = false;
            tetra.dosIntegration = DosIntegration::Tetrahedron;
            dumpBands("bands_tetrahedron.py", tetra);

            // The explicit-channel branch emits a different literal block from
            // the derive-them-yourself one.
            ElectronicConfig channels = symmetry;
            channels.symmetry.classifyLines = false;
            FatbandProjection carbonPz;
            carbonPz.label = "C p_z";
            carbonPz.atoms = {0, 1};
            carbonPz.angularMomentum = 1;
            carbonPz.magnetic = 1;
            FatbandProjection ironD;
            ironD.label = "Fe d";
            ironD.element = "Fe";
            ironD.angularMomentum = 2;
            channels.fatbandProjections = {carbonPz, ironD};
            dumpBands("bands_fatband_channels.py", channels);

            // The configuration the graphene reference benchmark runs end to
            // end against Kogan & Nazarov, PRB 85, 115418 (2012): the Γ-K-M-Γ
            // path with both the symmetry classification and a p_z / s
            // projection, so the π manifold can be identified by its orbital
            // character rather than guessed at from its energy. Only the
            // baseline path is substituted there — everything else about the
            // script is what ships.
            ElectronicConfig graphene;
            graphene.backend = ElectronicBackend::Gpaw;
            graphene.baselineDensityPath = "/jobs/proc_1/single_point.gpw";
            graphene.kpath = "GKMG";
            graphene.npoints = 31;
            graphene.pdos = false;
            graphene.bandSymmetry = true;
            graphene.fatbands = true;
            FatbandProjection pz;
            pz.label = "C p_z";
            pz.element = "C";
            pz.angularMomentum = 1;
            pz.magnetic = 1;
            FatbandProjection s;
            s.label = "C s";
            s.element = "C";
            s.angularMomentum = 0;
            graphene.fatbandProjections = {pz, s};
            dumpBands("bands_graphene_symmetry.py", graphene);
        }

        // Born effective charges: a nested-function script with an f-string
        // inside a conditional expression inside an f-string, which is exactly
        // the shape a hand-check misses.
        {
            const auto dumpBorn = [&dir](const std::string& name,
                                         const BornChargesConfig& config) {
                std::ofstream out(dir + "/" + name);
                out << generateBornChargesScript(config);
            };
            BornChargesConfig born;
            born.calculator = gpawConfig();
            dumpBorn("born_charges.py", born);
            BornChargesConfig subset = born;
            subset.atomIndices = {0, 2, 5};
            subset.acousticSumRule = false;
            dumpBorn("born_charges_subset.py", subset);
        }

        // Piezoelectric tensor: nested functions, a Calango-precomputed
        // strain-stencil literal, an einsum-based symmetrization and a
        // conditional e -> d block — dumped both without and with the
        // elastic-stiffness input, since that branch only appears with one.
        {
            const auto dumpPiezo = [&dir](const std::string& name,
                                          const PiezoelectricConfig& config) {
                std::ofstream out(dir + "/" + name);
                out << generatePiezoelectricScript(config);
            };
            PiezoelectricConfig piezo;
            piezo.calculator = gpawConfig();
            piezo.baselinePath = "/jobs/proc_1/single_point.gpw";
            dumpPiezo("piezoelectric.py", piezo);

            PiezoelectricConfig piezoRelaxed = piezo;
            piezoRelaxed.relaxIons = true;
            piezoRelaxed.pointsPerComponent = 4;
            dumpPiezo("piezoelectric_relaxed_4point.py", piezoRelaxed);

            PiezoelectricConfig piezoElastic = piezo;
            std::array<std::array<double, 6>, 6> stiffness{};
            for (int i = 0; i < 6; ++i)
                stiffness[static_cast<std::size_t>(i)][static_cast<std::size_t>(i)] = 300.0;
            piezoElastic.elasticStiffnessGpa = stiffness;
            dumpPiezo("piezoelectric_with_elastic.py", piezoElastic);
        }

        // Raman / IR: nested functions, an einsum-heavy numerical body and a
        // multi-line f-string in the result marker. Both branches are dumped
        // because the Raman half is a large chunk of code that only appears
        // when the toggle is on.
        {
            const auto dumpRamanIr = [&dir](const std::string& name,
                                            const RamanIrConfig& config) {
                std::ofstream out(dir + "/" + name);
                out << generateRamanIrScript(config);
            };
            RamanIrConfig raman;
            raman.calculator = gpawConfig();
            raman.baselinePath = "/tmp/baseline/single_point.gpw";
            raman.bornChargesPath = "/tmp/born/born_charges.json";
            dumpRamanIr("raman_ir.py", raman);
            RamanIrConfig withOptics = raman;
            withOptics.opticsPath = "/tmp/optics/optics.json";
            dumpRamanIr("raman_ir_optics.py", withOptics);
            RamanIrConfig irOnly = raman;
            irOnly.computeRaman = false;
            dumpRamanIr("raman_ir_ironly.py", irOnly);

            // The two self-contained engines. Each emits a different parser —
            // an OUTCAR table and a Quantum ESPRESSO .dyn file — full of
            // regexes with backslash escapes that have to survive the trip
            // through a C++ string literal, which is exactly what a byte
            // compile catches and a read-through does not.
            RamanIrConfig vasp = raman;
            vasp.calculator.calculator = CalculatorKind::Vasp;
            vasp.calculator.vaspPotcarPath = "/opt/vasp/potpaw";
            vasp.baselinePath.clear();
            vasp.bornChargesPath.clear();
            dumpRamanIr("raman_ir_vasp.py", vasp);
            RamanIrConfig vaspIr = vasp;
            vaspIr.computeRaman = false;
            dumpRamanIr("raman_ir_vasp_ironly.py", vaspIr);

            RamanIrConfig qe = vasp;
            qe.calculator.calculator = CalculatorKind::QuantumEspresso;
            qe.calculator.espressoPseudoDir = "/opt/qe/pseudo";
            qe.calculator.qeEcutrhoRy = 320.0;
            dumpRamanIr("raman_ir_espresso.py", qe);
            RamanIrConfig qeIr = qe;
            qeIr.computeRaman = false;
            dumpRamanIr("raman_ir_espresso_ironly.py", qeIr);
        }

        // Nonlinear optics: each response contributes its own loop, and the
        // post-processing block is a run of nested f-strings and dict indexing
        // that only a byte compile settles.
        {
            const auto dumpNlopt = [&dir](const std::string& name,
                                          const NonlinearOpticsConfig& config) {
                std::ofstream out(dir + "/" + name);
                out << generateNonlinearOpticsScript(config);
            };
            NonlinearOpticsConfig shg;
            shg.calculator = gpawConfig();
            dumpNlopt("nlopt_shg.py", shg);

            NonlinearOpticsConfig everything = shg;
            everything.computeShift = true;
            everything.computeLinear = true;
            everything.components = {"yyy", "xxy", "zzz"};
            everything.gauge = NlOpticsGauge::Velocity;
            everything.scissorsEv = 0.7;
            everything.bandsFirst = 4;
            everything.bandsLast = -8;
            everything.vacuumAxis = 2;
            dumpNlopt("nlopt_all.py", everything);

            NonlinearOpticsConfig shiftOnly = shg;
            shiftOnly.computeShg = false;
            shiftOnly.computeShift = true;
            dumpNlopt("nlopt_shift.py", shiftOnly);
        }

        // The MLWF scripts, one per fixed-state mode, plus the interpolation
        // with and without its windows. Every branch emits different Python
        // around the Wannier() call, and a byte-compile is what catches an
        // indentation slip in a keyword block that only one mode produces.
        {
            const auto dumpWannier = [&dir](const std::string& name,
                                            const WannierConfig& config) {
                std::ofstream out(dir + "/" + name);
                out << generateWannierScript(config);
            };
            WannierConfig plain;
            plain.calculator = gpawConfig();
            dumpWannier("wannier_nwannier.py", plain);
            WannierConfig window = plain;
            window.fixedMode = WannierConfig::FixedStatesMode::EnergyWindow;
            window.energyWindowEv = 2.5;
            dumpWannier("wannier_fixedenergy.py", window);
            WannierConfig bands = plain;
            bands.fixedMode = WannierConfig::FixedStatesMode::BandCount;
            bands.fixedStates = 6;
            dumpWannier("wannier_fixedstates.py", bands);

            const auto dumpInterp =
                [&dir](const std::string& name,
                       const WannierInterpolationConfig& config) {
                    std::ofstream out(dir + "/" + name);
                    out << generateWannierInterpolationScript("/jobs/mlwf",
                                                              config);
                };
            WannierInterpolationConfig openWindows;
            dumpInterp("wannier_interp.py", openWindows);
            WannierInterpolationConfig bounded;
            bounded.useInnerWindow = true;
            bounded.innerWindowEv = 1.5;
            bounded.useOuterWindow = true;
            bounded.outerWindowEv = 8.0;
            dumpInterp("wannier_interp_windows.py", bounded);
        }

        // The CALPHAD equilibrium script. Dumped in both of its shapes: the
        // binary and ternary branches share only the preamble, so one dump
        // would leave half the generated Python unchecked — and this is the
        // one script in the project that can never be smoke-tested by running
        // it, since pycalphad is in no Calango environment.
        {
            CalphadScriptConfig binary;
            binary.components = {"AL", "ZN"};
            binary.phases = {"FCC_A1", "LIQUID"};
            binary.axisElement = "ZN";
            dumpText("calphad_binary.py",
                     CalphadScriptGenerator::generate(binary));
            CalphadScriptConfig ternary = binary;
            ternary.ternary = true;
            ternary.components = {"AL", "MG", "ZN"};
            ternary.secondAxisElement = "MG";
            dumpText("calphad_ternary.py",
                     CalphadScriptGenerator::generate(ternary));
        }

        std::printf("scripts written to %s\n", dir.c_str());
        return EXIT_SUCCESS;
    }

    // -- Generated scripts are self-contained -------------------------------
    //
    // The contract this pins is the reason the generator exists: a user copies
    // run.py to a cluster that has ASE and their calculator and nothing else,
    // and it runs. Anything Calango-private in the text — an import of a
    // helper module above all — silently breaks that, and breaks it at run
    // time on the remote machine rather than here.
    std::printf("Self-contained script logging:\n");
    {
        const std::string script =
            AseScriptGenerator::generate(CalculatorConfig{}, "structure.extxyz");
        check(!contains(script, "calango_log"),
              "no import of the retired calango_log helper module");
        check(!contains(script, "CalangoLog"),
              "no reference to the retired CalangoLog class");
        // The logger it replaced it with: a dict, three functions, stdlib only.
        checkContains(script, "def _calango_metric(",
                      "metric() is defined in the script itself");
        checkContains(script, "def _calango_progress(",
                      "progress() is defined in the script itself");
        checkContains(script, "def _calango_event(",
                      "event() is defined in the script itself");
        checkContains(script, "_json.dump",
                      "the JSON writing is inline");
        checkContains(script, "captureWarnings",
                      "warning routing to warnings.log is inline");
        // The on-disk contract the Results panel polls must not have moved.
        checkContains(script, "metrics.json", "still writes metrics.json");
        checkContains(script, "log.json", "still writes log.json");
        // Standard library only. Named individually rather than checked as a
        // set, so adding a third-party import to the block that is supposed to
        // be dependency-free has to go through this test first.
        for (const char* module : {"import json as _json", "import os as _os",
                                   "import logging as _logging",
                                   "import threading as _threading",
                                   "import warnings as _warnings"}) {
            checkContains(script, module,
                          std::string("logging block imports ") + module);
        }
    }

    // -- MACE parameter expansion -------------------------------------------
    std::printf("MACE calculator block:\n");
    {
        CalculatorConfig c = maceConfig();
        c.macePrecision = MacePrecision::Float32;
        c.maceDevice = "cuda";
        const std::string script = AseScriptGenerator::calculatorSnippet(c);
        checkContains(script, "default_dtype=\"float32\"", "float32 is honored");
        checkContains(script, "device=\"cuda\"", "device is honored");
        checkContains(script, "model=\"medium\"", "size keyword used with no file");
    }
    {
        CalculatorConfig c = maceConfig();
        c.macePrecision = MacePrecision::Float64;
        const std::string script = AseScriptGenerator::calculatorSnippet(c);
        checkContains(script, "default_dtype=\"float64\"", "float64 is honored");
    }
    {
        // A weights file pins the foundation entry point to that checkpoint.
        CalculatorConfig c = maceConfig();
        c.maceSource = MaceModelSource::FoundationOFF;
        c.maceModelPath = "/models/mace-off23-small.model";
        const std::string script = AseScriptGenerator::calculatorSnippet(c);
        checkContains(script, "mace_off", "MACE-OFF entry point");
        checkContains(script, "model=r\"/models/mace-off23-small.model\"",
                      "weights file overrides the size keyword");
        check(!contains(script, "model=\"medium\""),
              "size keyword dropped when a file is pinned");
    }
    {
        CalculatorConfig c = maceConfig();
        c.maceSource = MaceModelSource::CustomFile;
        c.maceModelPath = "/models/fine_tuned.pt";
        const std::string script = AseScriptGenerator::calculatorSnippet(c);
        checkContains(script, "MACECalculator", "custom checkpoint calculator");
        checkContains(script, "model_paths=r\"/models/fine_tuned.pt\"",
                      "custom weights path");
        check(!contains(script, "dispersion="),
              "custom checkpoints take no dispersion flag");
    }
    {
        // MP-0's Dispersion checkbox maps to mace_mp's constructor argument,
        // off by default; mace_off never receives the flag.
        CalculatorConfig c = maceConfig();
        checkContains(AseScriptGenerator::calculatorSnippet(c),
                      "dispersion=False", "dispersion defaults to off");
        c.maceDispersion = true;
        checkContains(AseScriptGenerator::calculatorSnippet(c),
                      "dispersion=True", "dispersion flag reaches mace_mp");
        c.maceSource = MaceModelSource::FoundationOFF;
        check(!contains(AseScriptGenerator::calculatorSnippet(c),
                        "dispersion="),
              "mace_off takes no dispersion flag");
    }

    // -- GPAW parameter expansion -------------------------------------------
    std::printf("GPAW calculator block:\n");
    {
        CalculatorConfig c = gpawConfig();
        c.gpawMode = GpawMode::PlaneWave;
        c.planeWaveCutoffEv = 450.0;
        c.gpawXc = "r2SCAN";
        c.gpawEigensolver = GpawEigensolver::RmmDiis;
        c.gpawMixer = GpawMixerKind::MixerDif;
        c.gpawMixerBeta = 0.02;
        c.gpawMixerNmaxold = 8;
        c.gpawMixerWeight = 100.0;
        c.gpawConvDensity = 1e-5;
        c.scfMaxSteps = 333;
        c.kpts[0] = 6;
        c.kpts[1] = 6;
        c.kpts[2] = 2;
        const std::string script = AseScriptGenerator::calculatorSnippet(c);
        checkContains(script, "from gpaw import GPAW, PW, MixerDif",
                      "imports exactly the symbols used");
        checkContains(script, "mode=PW(450)", "plane-wave cutoff");
        checkContains(script, "xc=\"r2SCAN\"", "xc functional");
        checkContains(script, "kpts=(6, 6, 2)", "Monkhorst-Pack grid");
        checkContains(script, "eigensolver=\"rmm-diis\"", "eigensolver");
        checkContains(script, "mixer=MixerDif(0.02, 8, 100)",
                      "mixer class + beta/nmaxold/weight");
        checkContains(script, "\"density\": 1e-05", "density convergence");
        checkContains(script, "maxiter=333", "SCF iteration cap");
        check(!contains(script, "h="), "no FD grid spacing in PW mode");
        check(!contains(script, "basis="), "no LCAO basis in PW mode");
    }
    {
        CalculatorConfig c = gpawConfig();
        c.gpawMode = GpawMode::FiniteDifference;
        c.gpawGridSpacing = 0.18;
        const std::string script = AseScriptGenerator::calculatorSnippet(c);
        checkContains(script, "mode=\"fd\"", "FD mode");
        checkContains(script, "h=0.18", "grid spacing");
        // PW must not be imported when it is not used.
        check(!contains(script, "PW"), "PW not imported in FD mode");
    }
    {
        CalculatorConfig c = gpawConfig();
        c.gpawMode = GpawMode::Lcao;
        c.gpawBasis = "dzp";
        c.spinPolarized = true;
        c.gpawMixer = GpawMixerKind::MixerSum;
        const std::string script = AseScriptGenerator::calculatorSnippet(c);
        checkContains(script, "mode=\"lcao\"", "LCAO mode");
        checkContains(script, "basis=\"dzp\"", "LCAO basis");
        checkContains(script, "spinpol=True", "spin polarization");
        checkContains(script, "MixerSum", "spin-aware mixer");
    }
    {
        // GPAW is fully parameterized, so it must not also get the
        // "wire these in yourself" hand-off comment the other DFT hooks do.
        CalculatorConfig c = gpawConfig();
        const std::string script =
            AseScriptGenerator::generate(c, "structure.extxyz");
        check(!contains(script, "apply in the calculator block above"),
              "no redundant hand-off comment for GPAW");

        CalculatorConfig vasp = c;
        vasp.calculator = CalculatorKind::Vasp;
        checkContains(AseScriptGenerator::generate(vasp, "structure.extxyz"),
                      "apply in the calculator block above",
                      "other DFT hooks keep the hand-off comment");
    }

    // -- Geometry constraints -----------------------------------------------
    //
    // The failure this guards against is silent: a constraint that reaches the
    // script in the wrong ASE spelling, or after the cell filter has already
    // wrapped `atoms`, leaves the run looking normal while the atoms the user
    // pinned move anyway.
    std::printf("Geometry constraints:\n");
    {
        const std::string script =
            AseScriptGenerator::generate(constrainedRelaxation(),
                                         "structure.extxyz");
        checkContains(script, "from ase.constraints import FixAtoms, FixCartesian",
                      "imports both constraint classes");
        checkContains(script, "FixAtoms(indices=[0, 1, 2])",
                      "fully frozen atoms become FixAtoms");
        // ASE's mask is true for the coordinates that are HELD — the opposite
        // of the intuitive reading, and the bug this pins.
        checkContains(script, "mask=(False, False, True)",
                      "a partial mask becomes FixCartesian, held-directions true");
        checkContains(script, "_p[2] > 5",
                      "a region rule keeps its lower bound");
        checkContains(script, "_p[2] < 10",
                      "and its upper bound");
        checkContains(script, "enumerate(atoms.get_positions())",
                      "the region is re-evaluated at run time, not baked in");
        checkContains(script, "atoms.set_constraint(_constraints)",
                      "the rules are bound to the atoms");

        // Order matters: constraints must reach `atoms` before the optimizer.
        check(script.find("atoms.set_constraint") < script.find("opt = BFGS("),
              "constraints are applied before the optimizer is built");
    }
    {
        // With a cell filter the ordering is what decides whether the frozen
        // atoms are frozen at all — the filter forwards whatever forces it is
        // handed.
        CalculatorConfig c = constrainedRelaxation();
        c.relaxCell = true;
        const std::string script =
            AseScriptGenerator::generate(c, "structure.extxyz");
        check(script.find("atoms.set_constraint") < script.find("_CellFilter(atoms"),
              "constraints are applied before the cell filter wraps the atoms");
    }
    {
        // A rule that freezes nothing, and an index rule with no atoms, are
        // both leftovers of an emptied dialog row. Emitting them would put a
        // constraint block in the script that constrains nothing.
        CalculatorConfig c = gpawConfig();
        c.task = TaskKind::GeometryOptimization;
        GeometryConstraint empty;
        empty.indices = {4};
        empty.fix[0] = empty.fix[1] = empty.fix[2] = false;
        GeometryConstraint noAtoms; // Indices selection, empty list
        c.constraints = {empty, noAtoms};
        const std::string script =
            AseScriptGenerator::generate(c, "structure.extxyz");
        check(!contains(script, "set_constraint"),
              "freeze-nothing and empty rules emit no constraint block");
    }
    {
        CalculatorConfig c = gpawConfig();
        c.task = TaskKind::GeometryOptimization;
        const std::string script =
            AseScriptGenerator::generate(c, "structure.extxyz");
        check(!contains(script, "ase.constraints"),
              "an unconstrained relaxation imports nothing extra");
    }

    // -- GPAW API drift (25.7 vs 26.7) ---------------------------------------
    //
    // GPAW 26 made its NEW engine the default, which removed three APIs the
    // generated scripts called. Each failure was silent or fatal in a
    // different way, and all three were found by running the full mode matrix
    // against both installed GPAWs.
    std::printf("Custom LCAO basis sets:\n");
    {
        CalculatorConfig lcao;
        lcao.calculator = CalculatorKind::Gpaw;
        lcao.gpawMode = GpawMode::Lcao;
        lcao.gpawBasis = "my-tz2p";
        lcao.gpawBasisDir = "/opt/basis";
        const std::string imports = AseScriptGenerator::gpawImports(lcao);
        checkContains(imports, "setup_paths.insert(0, _basis_dir)",
                      "the custom directory is PREPENDED to the search path");
        // Assignment instead of insertion would find the basis and lose the
        // PAW datasets, which is a much worse failure than not finding it.
        check(!contains(imports, "setup_paths ="),
              "and never assigned over — that would drop the PAW datasets");
        checkContains(imports, "/opt/basis",
                      "the configured directory reaches the script");
        checkContains(imports, "CALANGO_WARN",
                      "a missing directory is reported rather than silent");

        // Nothing configured, or a mode with no basis at all: no block.
        CalculatorConfig plain = lcao;
        plain.gpawBasisDir.clear();
        check(!contains(AseScriptGenerator::gpawImports(plain), "setup_paths"),
              "no directory configured emits no setup-path block");
        CalculatorConfig pw = lcao;
        pw.gpawMode = GpawMode::PlaneWave;
        check(!contains(AseScriptGenerator::gpawImports(pw), "setup_paths"),
              "and neither does a plane-wave run, which has no LCAO basis");
    }

    std::printf("PDOS / PhDOS smearing is no longer a run parameter:\n");
    {
        ElectronicConfig bands;
        bands.backend = ElectronicBackend::Gpaw;
        bands.pdos = true;
        const std::string script = generateElectronicScript(bands);
        // Sigma moved to the viewer. A width still written here would be the
        // one baked into the stored curve, which is exactly what was removed.
        check(!contains(script, "pdos_width"),
              "the electronic script sets no Gaussian width");
        // The flag became an expression when the tetrahedron option landed:
        // a sampled run still writes False, a tetrahedron run True, and the
        // script decides which at run time.
        checkContains(script,
                      "\"broadened\": pdos_integration == \"tetrahedron\"",
                      "and marks the histogram it wrote as unbroadened unless "
                      "it was integrated with tetrahedra");
    }

    std::printf("GPAW 25.7 / 26.7 compatibility:\n");
    {
        ElectronicConfig bands;
        bands.backend = ElectronicBackend::Gpaw;
        bands.pdos = true;
        const std::string script = generateElectronicScript(bands);
        // get_orbital_ldos is gone from the new engine; every projection was
        // swallowed by `except Exception: continue`, so the run "succeeded"
        // with no PDOS at all. DOSCalculator is present in both engines.
        checkContains(script, "from gpaw.dos import DOSCalculator",
                      "PDOS goes through the portable DOSCalculator");
        // Its raw_pdos() returns a BROADENED curve, so it cannot be the entry
        // point any more: the smearing moved to the viewer and the run has to
        // write the unbroadened weights. pdos_weights + get_projector_numbers
        // are the same pair the fatband block already relies on, and are
        // present in both engines.
        checkContains(script, "pdos_weights(",
                      "raw projection weights, not a pre-broadened curve");
        checkContains(script, "get_projector_numbers",
                      "shells resolved per species rather than assumed");
        checkContains(script, "bincount(",
                      "the weights are binned into a histogram in the run");
        checkContains(script,
                      "\"broadened\": pdos_integration == \"tetrahedron\"",
                      "and flagged for the viewer as something it may broaden "
                      "(a sampled histogram) rather than something it may not");

        // Tetrahedron integration: a different Brillouin-zone integral, and
        // the viewer has to be told which one it is holding.
        {
            ElectronicConfig tetra = bands;
            tetra.dosIntegration = DosIntegration::Tetrahedron;
            const std::string tetraScript = generateElectronicScript(tetra);
            checkContains(tetraScript, "pdos_tetrahedron = True",
                          "the tetrahedron request reaches the script");
            checkContains(tetraScript, "width=0.0",
                          "as width=0.0, which is what selects linear "
                          "tetrahedron interpolation in gpaw.dos");
            checkContains(tetraScript, "raw_pdos(pdos_energies",
                          "through DOSCalculator.raw_pdos");
            checkContains(tetraScript, "\"integration\": pdos_integration",
                          "and the run records which integration it used");
            checkContains(tetraScript, "CALANGO_WARN tetrahedron integration",
                          "with a loud fallback when the mesh cannot support "
                          "the interpolation");
            // The branch is chosen at RUN time, not generation time: the block
            // is always emitted and `pdos_tetrahedron` decides. So the claim
            // about a sampled run is about the flag, not about the absence of
            // the code — which is also what makes the fallback possible when
            // the mesh turns out to be unusable.
            checkContains(script, "pdos_tetrahedron = False",
                          "while sampling stays the default");
            checkContains(script, "if pdos_tetrahedron:",
                          "and the tetrahedron block is entered only when the "
                          "flag says so");
        }
        checkContains(script, "shift_fermi_level=False",
                      "keeping energies on the same scale as efermi");
        checkContains(script, "no PDOS projections were produced",
                      "an empty PDOS is now reported, not silent");
    }
    // The standalone ELF generator is gone: ELF is one of the six fields the
    // single-point density-export block writes, and its API is pinned in the
    // "GPAW density exports" block further down. One generator, one place the
    // gpaw.elf entry point is named.
    {
        // Symmetry is detected once from the starting geometry and validated
        // against every later one. MD's thermal velocities and a phonon
        // displacement both break it, killing the run on the first step.
        CalculatorConfig md;
        md.calculator = CalculatorKind::Gpaw;
        md.task = TaskKind::MolecularDynamics;
        checkContains(AseScriptGenerator::calculatorSnippet(md),
                      "symmetry=\"off\"",
                      "MD forces symmetry off");
        PhononConfig phonon;
        phonon.calculator = gpawConfig();
        checkContains(PhononScriptGenerator::generate(phonon, "s.extxyz"),
                      "symmetry=\"off\"",
                      "and so do finite-displacement phonons");
        // Geometry optimization must NOT: relaxation follows symmetric forces
        // and stays inside its group, so folding the k-points is a real saving.
        CalculatorConfig opt = gpawConfig();
        opt.task = TaskKind::GeometryOptimization;
        check(!contains(AseScriptGenerator::calculatorSnippet(opt),
                        "symmetry=\"off\""),
              "but geometry optimization keeps symmetry on");
    }

    // -- LO-TO splitting ----------------------------------------------------
    std::printf("LO-TO splitting (non-analytical term correction):\n");
    {
        PhononConfig plain;
        plain.kpath = "GXMG";
        check(!plain.loToSplitting(), "off without a Born charges file");
        const std::string bare = PhononScriptGenerator::generate(plain, "s.extxyz");
        check(!contains(bare, "nac_params"),
              "no correction is emitted when none was asked for");

        PhononConfig loto = plain;
        loto.bornChargesFile = "/tmp/born_charges.json";
        for (int i = 0; i < 3; ++i)
            loto.dielectric[i][i] = 2.96;
        check(loto.loToSplitting(), "on once a Born charges file is set");
        const std::string script = PhononScriptGenerator::generate(loto, "s.extxyz");

        checkContains(script, "phonon.nac_params", "nac_params is set on phonopy");
        checkContains(script, "/tmp/born_charges.json",
                      "the Born charges file is read");
        checkContains(script, "2.96", "the dielectric tensor is embedded");
        checkContains(script, "14.399652",
                      "phonopy's e^2/(4 pi eps_0) unit factor is supplied");
        // Z* must sum to zero over the cell; a residual is the Born run's own
        // convergence error and puts a spurious dipole on the acoustic modes.
        checkContains(script, "born_tensors -= _residual / len(atoms)",
                      "the acoustic sum rule is re-imposed on Z*");
        // The correction is a directional limit: at exactly q = 0 phonopy needs
        // to be told which way the path came in, or it returns TO twice.
        checkContains(script, "nac_q_direction",
                      "Gamma is re-evaluated with a direction of approach");
        checkContains(script, "lo_to_split_cm1",
                      "the splitting itself is reported as a result");
        // ase.phonons cannot apply the correction, so the driver choice is not
        // optional here - and a silent fallback would return a dispersion with
        // the splitting simply missing.
        checkContains(script, "run_symmetry_reduced_displacements()",
                      "the phonopy driver is selected even without symmetry "
                      "reduction");
        checkContains(script, "CALANGO_ERROR LO-TO splitting needs phonopy",
                      "and a missing phonopy is a hard error, not a fallback");
        check(!contains(bare, "CALANGO_ERROR LO-TO splitting needs phonopy"),
              "while a plain run still falls back to the ASE driver");
    }

    // -- Molecular-dynamics constraints -------------------------------------
    std::printf("Molecular dynamics constraints:\n");
    {
        CalculatorConfig md = constrainedRelaxation();
        md.task = TaskKind::MolecularDynamics;
        const std::string script =
            AseScriptGenerator::generate(md, "structure.extxyz");
        checkContains(script, "atoms.set_constraint(_constraints)",
                      "MD honours the same constraint rules as a relaxation");
        // Order matters: ASE's MaxwellBoltzmannDistribution consults the
        // constraints and leaves frozen degrees of freedom at zero, so a held
        // substrate starts at rest instead of being handed thermal velocities.
        // Compared against the CALL, not the import at the top of the file.
        check(script.find("atoms.set_constraint")
                  < script.find("MaxwellBoltzmannDistribution(atoms"),
              "and applies them BEFORE the velocities are drawn");
    }
    {
        CalculatorConfig plain;
        plain.task = TaskKind::MolecularDynamics;
        const std::string script =
            AseScriptGenerator::generate(plain, "structure.extxyz");
        check(!contains(script, "ase.constraints"),
              "an unconstrained MD run imports nothing extra");
    }

    // -- Charged defects and CDD on VASP and Quantum ESPRESSO ----------------
    //
    // Both modules are engine-independent downstream: the defect diagram
    // consumes E_tot(q), E_VBM and E_corr(q); the CDD consumes three densities
    // on one grid. Only the acquisition of those changes.
    std::printf("Charged defects — external engines:\n");
    {
        DefectConfig vasp;
        vasp.calculator.calculator = CalculatorKind::Vasp;
        vasp.calculator.vaspPotcarPath = "/opt/potcars";
        vasp.pristinePath = "/runs/host";
        vasp.neutralDefectPath = "/runs/neutral";
        const std::string script = generateDefectScript(vasp);

        checkContains(script, "nelect=", "VASP sets the charge through NELECT");
        checkContains(script, "neutral_electrons - float(q)",
                      "and q = +1 is one electron FEWER — the sign that "
                      "inverts the whole diagram if it is wrong");
        checkContains(script, "lvhar=True",
                      "the electrostatic potential is written for the FNV "
                      "alignment");
        checkContains(script, "nsw=0",
                      "every charge state runs at the neutral geometry");
        checkContains(script, "def _nelect_per_atom()",
                      "ZVAL is read lazily — PRISTINE is defined further down "
                      "the file, so reading it at import time is a NameError "
                      "no syntax check catches");
        checkContains(script, "get_freysoldt_correction",
                      "the correction is delegated to pymatgen's "
                      "implementation");
        checkContains(script, "pymatgen is not available",
                      "and a missing pymatgen degrades to UNCORRECTED energies "
                      "rather than to a hand-rolled correction nobody checked");
        checkContains(script, "Formation energies",
                      "the shared arithmetic tail is still emitted");
        checkContains(script, "transition", "transition levels included");
    }
    {
        DefectConfig qe;
        qe.calculator.calculator = CalculatorKind::QuantumEspresso;
        qe.pristinePath = "/runs/host";
        qe.neutralDefectPath = "/runs/neutral";
        const std::string script = generateDefectScript(qe);
        checkContains(script, "'tot_charge': float(q)",
                      "QE's tot_charge follows the physical sign directly");
        checkContains(script, "read_espresso_out",
                      "energies come from the parsed pw.x output");
        checkContains(script, "Formation energies",
                      "and the same shared tail follows");
        check(!contains(script, "from gpaw import"),
              "with no GPAW import anywhere in a QE run");
    }

    std::printf("Charge density difference — external engines:\n");
    {
        CddRunConfig vasp;
        vasp.calculator.calculator = CalculatorKind::Vasp;
        vasp.baselineDir = "/runs/parent";
        vasp.subsystemB = {2, 3};
        vasp.allElectron = true;
        const std::string script = CddScriptGenerator::generate(vasp);

        checkContains(script, "AECCAR0",
                      "all-electron CDD reads the AECCAR pair");
        checkContains(script, "ngxf=_ngx, ngyf=_ngy, ngzf=_ngz",
                      "and pins the fragment FFT grid to the parent's — VASP "
                      "picks it from the cell CONTENTS, so a fragment can "
                      "silently land on a different one");
        checkContains(script, "ispin=2",
                      "fragments are spin-polarized: half a closed shell is "
                      "open-shell");
        checkContains(script, "rho_ab - rho_a - rho_b",
                      "the subtraction itself is unchanged");
        check(!contains(script, "from gpaw import GPAW"),
              "and no GPAW restart is attempted");
    }
    {
        CddRunConfig qe;
        qe.calculator.calculator = CalculatorKind::QuantumEspresso;
        qe.baselineDir = "/runs/parent";
        qe.subsystemB = {1};
        const std::string script = CddScriptGenerator::generate(qe);
        checkContains(script, "plot_num = 0",
                      "QE exports its density through pp.x");
        checkContains(script, "system['nr1'], system['nr2'], system['nr3'] = _grid",
                      "with the fragment grid pinned to the parent's at run "
                      "time, not baked in when the script was written");
        checkContains(script, "CALANGO_PP_X",
                      "and pp.x is locatable when it is not on PATH");
    }
    {
        // GPAW is untouched: it still restarts from the .gpw, which is a
        // stronger guarantee than re-specifying settings could ever be.
        CddRunConfig gpaw;
        gpaw.calculator.calculator = CalculatorKind::Gpaw;
        gpaw.baselineDir = "/runs/parent";
        gpaw.subsystemB = {1};
        const std::string script = CddScriptGenerator::generate(gpaw);
        checkContains(script, "parent = GPAW(_gpw[0]",
                      "GPAW still restarts from the parent .gpw");
        checkContains(script, "params = dict(_p.todict())",
                      "and reads its exact parameters back out of it");
        check(!contains(script, "ngxf"),
              "with no grid pinning needed — the restart already fixes it");
    }

    // -- Born effective charges on VASP and Quantum ESPRESSO -----------------
    //
    // Both compute Z* by DFPT in ONE run rather than by 6N displaced SCFs.
    // That is not only cheaper: it is an analytic derivative, so there is no
    // displacement amplitude to trade off against SCF noise. The output schema
    // is deliberately identical to the GPAW path's, because the viewer, the
    // phonon LO-TO block and every test downstream read one file format.
    std::printf("Born charges — DFPT engines:\n");
    {
        BornChargesConfig vasp;
        vasp.calculator.calculator = CalculatorKind::Vasp;
        vasp.calculator.vaspPotcarPath = "/opt/potcars";
        vasp.calculator.vaspXc = "PBEsol";
        const std::string script = generateBornChargesScript(vasp);

        checkContains(script, "lepsilon=True",
                      "VASP takes the DFPT linear-response route");
        check(!contains(script, "displaced") && !contains(script, "delta"),
              "and displaces nothing — no finite difference anywhere in it, "
              "header comment included");
        checkContains(script, "DENSITY-FUNCTIONAL PERTURBATION THEORY",
                      "the preamble describes the route actually taken");
        checkContains(script, "ediff=1e-8",
                      "with a tight ground state, since the response is its "
                      "derivative");
        checkContains(script, "lreal=False",
                      "LREAL=Auto is unsupported with LEPSILON and is pinned off");
        checkContains(script, "BORN EFFECTIVE CHARGES",
                      "the OUTCAR block is the one parsed");
        checkContains(script, "MACROSCOPIC STATIC DIELECTRIC TENSOR",
                      "and the dielectric tensor comes free with the same run");
        checkContains(script, "/opt/potcars", "the POTCAR library is exported");
        checkContains(script, "born_charges.json",
                      "writing the same result file the GPAW path writes");
        checkContains(script, "'acoustic_sum_rule'",
                      "in the same schema, sum rule and all");
        checkContains(script, "SEMICONDUCTOR",
                      "and a metal is refused with the reason, not a stack "
                      "trace");
    }
    {
        BornChargesConfig qe;
        qe.calculator.calculator = CalculatorKind::QuantumEspresso;
        qe.calculator.qeEcutwfcRy = 70.0;
        const std::string script = generateBornChargesScript(qe);

        checkContains(script, "epsil = .true.",
                      "QE asks ph.x for the electric-field response");
        checkContains(script, "fildyn = 'calango.dyn'",
                      "with a named dynamical-matrix file");
        checkContains(script, "'ecutwfc': 70",
                      "the ground state uses the configured cutoff");
        checkContains(script, "'conv_thr': 1e-12",
                      "converged far tighter than a single point would be");
        checkContains(script, "CALANGO_PH_X",
                      "ph.x is locatable when it is not on PATH");
        // ph.x prints the charges twice, with and without the sum rule. Taking
        // the first (raw) set is what leaves the ASR residual meaningful as a
        // convergence diagnostic instead of reporting a corrected zero.
        checkContains(script, "without the acoustic sum rule",
                      "the raw set is the one taken, so the ASR residual still "
                      "measures convergence");
        checkContains(script, "born_charges.json",
                      "and the same result file is written");
    }
    {
        // An engine that can do neither is refused up front, with the three
        // that can named — not after a long run fails at the response step.
        BornChargesConfig emt;
        emt.calculator.calculator = CalculatorKind::EMT;
        const std::string script = generateBornChargesScript(emt);
        checkContains(script, "raise RuntimeError",
                      "an engine with no polarization response is refused");
        checkContains(script, "VASP LEPSILON",
                      "and the message names what would work");
    }

    // -- Quantum ESPRESSO and SIESTA calculator blocks -----------------------
    //
    // Both used to be stubs driven off `planeWaveCutoffEv`. For QE that meant
    // one cutoff where the code needs two; for SIESTA it meant a plane-wave
    // cutoff for a code that HAS none — the number landed on `mesh_cutoff`,
    // so a user raising it to converge "the basis" refined a real-space grid
    // while the basis stayed exactly as small.
    std::printf("Quantum ESPRESSO calculator block:\n");
    {
        CalculatorConfig qe;
        qe.calculator = CalculatorKind::QuantumEspresso;
        qe.qeEcutwfcRy = 80.0;
        qe.qeEcutrhoRy = 640.0; // an 8x dual, as ultrasoft wants
        qe.qeInputDft = "pbesol";
        qe.qeOccupations = QeOccupations::Smearing;
        qe.qeSmearing = QeSmearing::MarzariVanderbilt;
        qe.qeDegaussRy = 0.02;
        qe.qeConvThrRy = 1e-10;
        qe.espressoPseudoDir = "/opt/pseudos/sssp";
        qe.kptsGammaCentered = true;
        const std::string script = AseScriptGenerator::calculatorSnippet(qe);

        checkContains(script, "\"ecutwfc\": 80", "ecutwfc is written in Ry");
        checkContains(script, "\"ecutrho\": 640",
                      "and so is ecutrho — the second grid QE actually has");
        checkContains(script, "\"input_dft\": \"pbesol\"",
                      "the functional is the one that was chosen");
        checkContains(script, "\"smearing\": \"marzari-vanderbilt\"",
                      "smearing is named in QE's own vocabulary");
        checkContains(script, "\"degauss\": 0.02", "with its width in Ry");
        checkContains(script, "\"conv_thr\": 1e-10",
                      "and conv_thr is the SCF threshold, not a GPAW one");
        checkContains(script, "/opt/pseudos/sssp",
                      "the configured pseudopotential library is named");
        checkContains(script, "koffset=(0, 0, 0)",
                      "Gamma-centering is QE's koffset, not a VASP tag");
        check(!contains(script, "planeWaveCutoff")
                  && !contains(script, "ENCUT") && !contains(script, "gpaw"),
              "and nothing VASP- or GPAW-shaped leaks into a QE block");
    }
    {
        // `fixed` and the tetrahedron methods take no width. Writing degauss
        // beside them is how a QE input ends up silently ignored or refused.
        CalculatorConfig insulator;
        insulator.calculator = CalculatorKind::QuantumEspresso;
        insulator.qeOccupations = QeOccupations::Fixed;
        const std::string script =
            AseScriptGenerator::calculatorSnippet(insulator);
        checkContains(script, "\"occupations\": \"fixed\"",
                      "fixed occupations are emitted");
        check(!contains(script, "degauss") && !contains(script, "smearing\":"),
              "and no smearing width is written alongside them");

        insulator.qeOccupations = QeOccupations::Tetrahedra;
        check(!contains(AseScriptGenerator::calculatorSnippet(insulator),
                        "degauss"),
              "nor alongside the tetrahedron method");

        // ecutrho left at zero means "QE's own 4x default" — the key is
        // omitted rather than written as a literal 0, which pw.x would reject.
        CalculatorConfig autoDual;
        autoDual.calculator = CalculatorKind::QuantumEspresso;
        autoDual.qeEcutrhoRy = 0.0;
        // Matched on the KEY, not the bare word: the block's comment explains
        // what ecutrho is and would satisfy a looser search whether or not the
        // key was emitted.
        check(!contains(AseScriptGenerator::calculatorSnippet(autoDual),
                        "\"ecutrho\":"),
              "an auto dual omits the ecutrho key instead of writing zero");
    }

    std::printf("SIESTA calculator block:\n");
    {
        CalculatorConfig siesta;
        siesta.calculator = CalculatorKind::Siesta;
        siesta.siestaXc = "PBEsol";
        siesta.siestaBasisType = SiestaBasisType::SplitGauss;
        siesta.siestaBasisSize = "TZP";
        siesta.siestaEnergyShiftEv = 0.05;
        siesta.siestaMeshCutoffEv = 450.0;
        siesta.siestaPseudoDir = "/opt/pseudos/psml";
        // Deliberately set, and deliberately expected NOT to appear: SIESTA
        // has no plane-wave cutoff, and this field used to become its mesh.
        siesta.planeWaveCutoffEv = 999.0;
        const std::string script = AseScriptGenerator::calculatorSnippet(siesta);

        checkContains(script, "xc=\"PBEsol\"", "the XC functional is written");
        checkContains(script, "basis_set=\"TZP\"", "so is the basis SIZE");
        checkContains(script, "\"PAO.BasisType\": \"splitgauss\"",
                      "and the basis TYPE, as an fdf argument");
        checkContains(script, "energy_shift=0.05",
                      "PAO.EnergyShift — the orbital confinement energy");
        checkContains(script, "mesh_cutoff=450",
                      "and MeshCutoff, which is the real-space grid");
        checkContains(script, "/opt/pseudos/psml",
                      "the configured pseudopotential library is named");
        check(!contains(script, "999"),
              "the plane-wave cutoff field does NOT reach SIESTA — that engine "
              "has no such parameter, and silently mapping it onto the mesh is "
              "the bug this replaces");
        check(!contains(script, "ENCUT") && !contains(script, "ecutwfc"),
              "and no other engine's cutoff appears either");
        checkContains(script, "kpts=[7, 7, 7]", "and the shared k-grid");

        // Spin: the TOP-LEVEL `spin=` keyword, not a lone "SpinPolarized"
        // fdf argument. ASE's own Siesta writer defaults `self.spin` to
        // "non-polarized" and unconditionally re-emits `Spin <self.spin>`
        // after the user's fdf_arguments block — so a script that only set
        // "SpinPolarized" inside fdf_arguments left every SIESTA run
        // non-polarized regardless of what the wizard's checkbox said. This
        // is the fix: the keyword actually read by ASE's Siesta calculator.
        checkNotContains(script, "SpinPolarized",
                         "the legacy standalone fdf key is gone — ASE "
                         "derives it from spin= itself now");
        checkContains(script, "spin=\"non-polarized\"",
                      "unpolarized is the explicit default, not silence");

        CalculatorConfig siestaSpin = siesta;
        siestaSpin.spinPolarized = true;
        siestaSpin.spinMode = SpinMode::Collinear;
        const std::string spinScript =
            AseScriptGenerator::calculatorSnippet(siestaSpin);
        checkContains(spinScript, "spin=\"collinear\"",
                      "a spin-polarized run reaches SIESTA through the "
                      "keyword ASE actually reads");

        CalculatorConfig siestaNonCollinear = siesta;
        siestaNonCollinear.spinMode = SpinMode::NonCollinear;
        const std::string ncScript =
            AseScriptGenerator::calculatorSnippet(siestaNonCollinear);
        checkContains(ncScript, "spin=\"non-collinear\"",
                      "and non-collinear spin gets its own distinct value");

        // A full single-point script: the pseudopotential-missing refusal,
        // and that output parsing is reached at all for this engine.
        CalculatorConfig noPseudo = siesta;
        noPseudo.siestaPseudoDir.clear();
        const std::string refused =
            AseScriptGenerator::generate(noPseudo, "structure.extxyz");
        checkContains(refused, "SIESTA_PP_PATH is not set",
                      "no pseudopotential directory: a clear message, not a "
                      "traceback from deep inside write_input()");
        checkContains(refused, "raise SystemExit",
                      "and the script refuses before ever invoking siesta");

        const std::string full =
            AseScriptGenerator::generate(siesta, "structure.extxyz");
        checkContains(full, "CALANGO_RESULT single_point=single_point.json",
                      "a configured run reaches the same result JSON every "
                      "engine writes — output parsing is calculator-agnostic");
    }

    // -- Simulated annealing ------------------------------------------------
    //
    // Three things can go wrong here and none of them is visible in a running
    // job: the ramp can end somewhere other than the requested final
    // temperature, the velocities can be seeded at the wrong end of it, and
    // the thermostat can be left at its construction setpoint while a schedule
    // is dutifully computed and thrown away. All three produce a run that
    // completes, writes a trajectory, and answers a different question.
    std::printf("Simulated annealing:\n");
    {
        // The law itself, evaluated in C++ — the same function the wizard's
        // preview and the generated script's expression are written from.
        for (const auto schedule : {AnnealingSchedule::Linear,
                                    AnnealingSchedule::Exponential,
                                    AnnealingSchedule::Logarithmic}) {
            const double at0 =
                annealingTemperature(schedule, 1000.0, 300.0, 3.0, 0.0);
            const double at1 =
                annealingTemperature(schedule, 1000.0, 300.0, 3.0, 1.0);
            check(std::abs(at0 - 1000.0) < 1e-9 && std::abs(at1 - 300.0) < 1e-9,
                  "every schedule is endpoint-exact: T(0) = T0, T(1) = T1");
            // Monotone in between, or the "cooling" run reheats halfway.
            bool monotone = true;
            double previous = at0;
            for (int i = 1; i <= 20; ++i) {
                const double value = annealingTemperature(
                    schedule, 1000.0, 300.0, 3.0, i / 20.0);
                monotone = monotone && value <= previous + 1e-9;
                previous = value;
            }
            check(monotone, "and monotone from the first step to the last");
        }
        // A vanishing coefficient is the linear ramp, not a division by zero.
        check(std::abs(annealingTemperature(AnnealingSchedule::Exponential,
                                            1000.0, 300.0, 0.0, 0.5)
                       - 650.0)
                  < 1e-9,
              "k -> 0 degenerates to the straight line, not to a NaN");
        // The two curved laws bend in opposite directions about the midpoint:
        // exponential front-loads the change, logarithmic back-loads it.
        const double mid = 650.0;
        check(annealingTemperature(AnnealingSchedule::Exponential, 1000.0,
                                   300.0, 3.0, 0.5)
                  < mid,
              "exponential is below the straight line at half-way (fast early)");
        check(annealingTemperature(AnnealingSchedule::Logarithmic, 1000.0,
                                   300.0, 3.0, 0.5)
                  < mid,
              "logarithmic is too — both crawl into the target");
    }
    {
        CalculatorConfig anneal;
        anneal.task = TaskKind::MolecularDynamics;
        anneal.ensemble = MdEnsemble::LangevinNVT;
        anneal.annealing = true;
        anneal.annealingSchedule = AnnealingSchedule::Exponential;
        anneal.annealStartK = 1200.0;
        anneal.annealEndK = 300.0;
        anneal.annealCoefficient = 4.0;
        anneal.temperatureK = 77.0; // must be IGNORED while annealing
        const std::string script =
            AseScriptGenerator::generate(anneal, "structure.extxyz");

        checkContains(script, "T_initial = 1200", "the ramp start is emitted");
        checkContains(script, "T_final = 300", "so is the ramp end");
        checkContains(script, "anneal_k = 4", "and the curvature");
        checkContains(script, "math.exp(-anneal_k * x)",
                      "the exponential law is the one written out");
        checkContains(script, "temperature_K = 1200",
                      "velocities are drawn at the START of the ramp, not at "
                      "the unrelated constant-temperature field");
        check(!contains(script, "temperature_K = 77"),
              "the constant-temperature setpoint is not emitted at all");
        checkContains(script, "def _set_target_temperature(value)",
                      "the thermostat is actually retargeted");
        checkContains(script, "interval=1)",
                      "every step, not once per sampling interval");
        checkContains(script, "thermostat._Q *= thermostat._kT / previous",
                      "and the Nose-Hoover chain's masses move with kT");
        checkContains(script, "target_temperature=_anneal_target(dyn.nsteps)",
                      "the setpoint is logged as its own metric series");
        check(!contains(script, "CALANGO_TARGET_TEMP"),
              "no single dashed reference line: the target is not constant");
        checkContains(script, "CALANGO_INFO annealing",
                      "the run announces the schedule it is following");
    }
    {
        // Each schedule writes its own expression, and only its own.
        CalculatorConfig anneal;
        anneal.task = TaskKind::MolecularDynamics;
        anneal.annealing = true;
        anneal.annealingSchedule = AnnealingSchedule::Logarithmic;
        const std::string script =
            AseScriptGenerator::generate(anneal, "structure.extxyz");
        checkContains(script, "math.log1p(anneal_k * x)",
                      "the logarithmic law is written out");
        check(!contains(script, "math.exp(-anneal_k"),
              "and the exponential one is not also present");

        anneal.annealingSchedule = AnnealingSchedule::Linear;
        const std::string linear =
            AseScriptGenerator::generate(anneal, "structure.extxyz");
        checkContains(linear, "T_initial + (T_final - T_initial) * x",
                      "linear is the plain interpolation");
    }
    {
        // NVE has no thermostat to retarget. The schedule is dropped rather
        // than emitted against an integrator that would ignore it.
        CalculatorConfig nve;
        nve.task = TaskKind::MolecularDynamics;
        nve.ensemble = MdEnsemble::VelocityVerletNVE;
        nve.annealing = true;
        const std::string script =
            AseScriptGenerator::generate(nve, "structure.extxyz");
        check(!contains(script, "_anneal_target"),
              "annealing is not generated for NVE, which has no setpoint");
        check(!contains(script, "CALANGO_TARGET_TEMP"),
              "and NVE still reports no thermostat target");
    }
    {
        // The default: nothing about annealing leaks into an ordinary run.
        CalculatorConfig plain;
        plain.task = TaskKind::MolecularDynamics;
        const std::string script =
            AseScriptGenerator::generate(plain, "structure.extxyz");
        check(!contains(script, "_anneal_target")
                  && !contains(script, "import math"),
              "a constant-temperature run carries no schedule machinery");
        checkContains(script, "CALANGO_TARGET_TEMP",
                      "and keeps its single dashed reference line");
    }

    // -- GPAW symmetry tolerance --------------------------------------------
    //
    // Regression for a run that died at the first stress evaluation with
    //     SymmetryAnalysisBug: Sorry!  Try using spglib.standardize_cell(...)
    // GPAW resolves its symmetry tolerance as `1e-7 if backwards_compatible
    // else 1e-5`, and backwards_compatible still defaults to TRUE — so an
    // unqualified run inherits 1e-7, and an atom a few times 1e-7 A off a
    // symmetry point (pure numerical residue) aborts the calculation.
    std::printf("GPAW symmetry tolerance:\n");
    {
        const std::string script =
            AseScriptGenerator::calculatorSnippet(gpawConfig());
        checkContains(script, "symmetry={\"tolerance\": 1e-5}",
                      "the tolerance is pinned to GPAW's modern default");
        check(!contains(script, "symmetry=\"off\""),
              "and symmetry itself stays ON");
    }
    {
        // "Symmetry: off" must still win outright — and must not emit both.
        CalculatorConfig off = gpawConfig();
        off.gpawSymmetryOff = true;
        const std::string script = AseScriptGenerator::calculatorSnippet(off);
        checkContains(script, "symmetry=\"off\"", "the off switch is honored");
        check(!contains(script, "tolerance"),
              "and no tolerance is written alongside it");
    }
    {
        // Exactly one `symmetry=` keyword, always. Two would be a Python
        // SyntaxError, which is how the Born-charge script broke when the
        // tolerance was first added underneath its own `symmetry='off'`.
        const auto countSymmetryKwargs = [](const std::string& script) {
            int count = 0;
            for (std::size_t at = script.find("symmetry=");
                 at != std::string::npos; at = script.find("symmetry=", at + 1))
                ++count;
            return count;
        };
        BornChargesConfig born;
        born.calculator = gpawConfig();
        check(countSymmetryKwargs(generateBornChargesScript(born)) == 1,
              "the Born-charge calculator carries one symmetry keyword");
        checkContains(generateBornChargesScript(born), "symmetry=\"off\"",
                      "and it is 'off' — Berry phases need the unfolded BZ");
        check(countSymmetryKwargs(
                  AseScriptGenerator::calculatorSnippet(gpawConfig())) == 1,
              "and so does the plain GPAW block");
    }

    // -- GPAW density exports -----------------------------------------------
    std::printf("VASP INCAR generation:\n");
    {
        CalculatorConfig vasp;
        vasp.calculator = CalculatorKind::Vasp;
        vasp.task = TaskKind::SinglePoint;
        vasp.vaspPotcarPath = "/opt/vasp/POTCARs";
        vasp.smearing = SmearingMethod::FermiDirac;
        vasp.smearingWidthEv = 0.05;
        const std::string script =
            AseScriptGenerator::generate(vasp, "structure.extxyz");

        checkContains(script, "os.environ['VASP_PP_PATH'] = _potcar_root",
                      "the POTCAR directory is exported, not assumed");
        checkContains(script, "'potpaw_PBE', 'potpaw', 'potpaw_LDA'",
                      "and a flat POTCAR layout is detected");
        checkContains(script, "os.symlink(_potcar_root, _link)",
                      "then shimmed, since ASE cannot be pointed elsewhere");
        // ISMEAR is the one mapping a reader cannot verify by inspection.
        checkContains(script, "ismear=-1",
                      "Fermi-Dirac smearing maps to ISMEAR = -1");
        checkContains(script, "sigma=0.05", "with its width as SIGMA");
        checkContains(script, "nsw=0", "a single point takes no ionic steps");
        checkContains(script, "ibrion=-1", "and no ionic algorithm");
        checkContains(script, "ispin=1", "spin off unless asked for");
        checkContains(script, "lcharg=True", "CHGCAR is written by default");
        checkContains(script, "lwave=False", "WAVECAR is not");

        CalculatorConfig gaussian = vasp;
        gaussian.smearing = SmearingMethod::Gaussian;
        checkContains(AseScriptGenerator::generate(gaussian, "s.extxyz"),
                      "ismear=0", "Gaussian smearing maps to ISMEAR = 0");
        CalculatorConfig mp = vasp;
        mp.smearing = SmearingMethod::MethfesselPaxton;
        checkContains(AseScriptGenerator::generate(mp, "s.extxyz"),
                      "ismear=1", "Methfessel-Paxton maps to first-order MP");
        CalculatorConfig none = vasp;
        none.smearing = SmearingMethod::None;
        const std::string noSmearing =
            AseScriptGenerator::generate(none, "s.extxyz");
        // "None" is NOT the tetrahedron method: -5 fails outright on the
        // Gamma-only meshes that small test cells use, which is the first
        // thing anyone runs. A user who wants tetrahedron integration now has
        // its own entry in the method list and gets -4 / -5 from that.
        checkContains(noSmearing, "ismear=0",
                      "None uses a narrow Gaussian, not ISMEAR=-5");
        checkContains(noSmearing, "sigma=0.01", "with a small width");

        CalculatorConfig magnetic = vasp;
        magnetic.spinPolarized = true;
        magnetic.spinMode = SpinMode::Collinear;
        const std::string spin =
            AseScriptGenerator::generate(magnetic, "s.extxyz");
        checkContains(spin, "ispin=2", "a spin-polarized run sets ISPIN = 2");
        check(!contains(spin, "magmom="),
              "and does NOT restate MAGMOM — it rides on the structure");

        CalculatorConfig relax = vasp;
        relax.task = TaskKind::GeometryOptimization;
        relax.maxSteps = 60;
        relax.vaspIsif = 2;
        relax.relaxCell = true;
        // Explicitly VASP-driven: the DEFAULT is now the ASE optimizer, under
        // which writing NSW at all is the double-relaxation bug.
        relax.vaspRelaxDriver = VaspRelaxDriver::Vasp;
        const std::string relaxed =
            AseScriptGenerator::generate(relax, "s.extxyz");
        checkContains(relaxed, "nsw=60", "a relaxation gets its step budget");
        checkContains(relaxed, "isif=3",
                      "and a variable-cell run is raised to ISIF = 3");

        CalculatorConfig unset = vasp;
        unset.vaspPotcarPath.clear();
        check(!contains(AseScriptGenerator::generate(unset, "s.extxyz"),
                        "VASP_PP_PATH'] ="),
              "with no directory configured the environment's own is left "
              "alone");
    }

    std::printf("Unfolding commensurability guard:\n");
    {
        UnfoldingConfig unfold;
        unfold.kpath = "GXWKG";
        const std::string script = generateUnfoldingScript(unfold);
        // Relative, not absolute: 1e-3 Ang is tight on a 4 Ang cell and
        // meaningless on a 40 Ang slab, so the same number meant two things.
        checkContains(script, "residual / _scale > 0.02",
                      "the guard is relative to the cell size");
        check(!contains(script, "if residual > 1e-3:"),
              "and no longer an absolute Angstrom bound");
        checkContains(script, "Force commensurability",
                      "with the failure naming the way out");
        // A run that is commensurate only approximately still proceeds, but
        // says so: the projection assumes an exact relation.
        checkContains(script, "CALANGO_WARN the cells are commensurate only",
                      "an approximate relation is reported, not hidden");

        UnfoldingConfig tight = unfold;
        tight.commensurateTolerance = 1e-3;
        checkContains(generateUnfoldingScript(tight),
                      "residual / _scale > 0.001",
                      "and the wizard's tolerance reaches the script");
    }

    std::printf("Unfolding projection (four bugs that produced no error):\n");
    {
        // Every one of these was live, and NONE of them failed loudly. The
        // GPAW signature crashed; the other three would each have written a
        // plausible effective_bands.json full of zeros or of character
        // assigned to the wrong wavevector.
        //
        // Verified against GPAW 26.7.1b1 by running the generated script: a
        // pristine Al 2x2x2 supercell unfolds back onto the primitive band
        // structure to 1.0e-3 eV, and the partition identity holds to 8.9e-16.
        UnfoldingConfig unfold;
        const std::string script = generateUnfoldingScript(unfold);

        // (1) get_rank_and_index(k, s) takes the spin too. Called with the k
        // alone it raises TypeError before any physics happens.
        checkContains(script, "get_rank_and_index(k_supercell_index, spin)",
                      "the k-point descriptor is asked for a spin as well");

        // (2) GPAW returns G in 1/Bohr; ASE's cell.reciprocal() is 1/Ang.
        // Mixing them scales every coordinate by 1.8897, so no coordinate is
        // ever an integer, the mask selects nothing and every weight is 0.0.
        checkContains(script, "from ase.units import Bohr",
                      "the 1/Bohr plane-wave vectors are converted");
        checkContains(script, "((g_bohr / Bohr) / (2.0 * np.pi))",
                      "before being expressed in the primitive basis");

        // (3) The acceptance test is against k - K, not against k. At Gamma
        // the two agree, which is exactly why a check run only there passes.
        checkContains(script, "folding_offset(matrix, k_primitive, k_supercell)",
                      "the mask is offset by what the folding removed");
        check(!contains(script, "np.round(g_primitive - k_primitive)"),
              "and no longer tests G against the primitive k itself");

        // (4) The projection lattice comes from the supercell, so the integer
        // test stays exact when the two cells agree only to a tolerance --
        // which is every relaxed defect cell. The error grows with |G|, so a
        // 0.05% mismatch discards the high-G half of every state.
        checkContains(script, "projection_cell = Cell(np.linalg.inv(M) @ atoms.cell[:])",
                      "the projection lattice is derived, not read");

        // The self-test that would have caught (2), (3) and (4) at runtime.
        // Sampled at three k, because at Gamma alone it cannot see (3).
        checkContains(script, "check_partition",
                      "the run checks that the weights partition the basis");
        checkContains(script,
                      "_samples = sorted({0, len(kpts_primitive) // 2,",
                      "at more than one k-point");
        // Matched on one emitted line: the message continues across an
        // f-string break, so the sentence is not contiguous in the source.
        checkContains(script, "That is an identity, not a",
                      "and refuses to write a map when the identity fails");

        // Spin. An NV centre is a triplet; unfolding only channel 0 would
        // silently report half the states.
        checkContains(script, "for spin in range(nspins):",
                      "both spin channels are projected");
        checkContains(script, "CALANGO_WARN spin-polarized",
                      "and the map says it sums them");
    }

    // -- Band symmetry classification ---------------------------------------
    //
    // The physics that cannot be checked by reading the script: the character
    // of a symmetry operation on a Bloch state is a permutation of the
    // plane-wave coefficients TIMES a phase, and getting either wrong yields
    // characters that still look like plausible small numbers. These pin the
    // pieces that a later refactor could quietly drop.
    std::printf("Band symmetry classification:\n");
    {
        ElectronicConfig config;
        config.backend = ElectronicBackend::Gpaw;
        config.baselineDensityPath = "/jobs/proc_1/single_point.gpw";
        check(!contains(generateElectronicScript(config), "band_symmetry.json"),
              "no classification unless it was asked for");

        config.bandSymmetry = true;
        const std::string script = generateElectronicScript(config);
        checkContains(script, "band_symmetry.json", "the result file is written");
        checkContains(script, "_calango_little_group",
                      "the little group is selected per k-point");
        checkContains(script, "_calango_point_group",
                      "and its character table computed, not looked up");
        // The origin matters at a zone boundary: spglib reports the operations
        // in whatever cell it was handed, and ase.build.graphene() puts an atom
        // at the origin where the site symmetry is -6m2, not the 6/mmm centre.
        checkContains(script, "_calango_symmetry_origin",
                      "the symmetry centre is located before classifying");
        // The character is <psi|{R|t}|psi>, computed as a coefficient
        // permutation plus a phase. Both halves must survive.
        checkContains(script, "rinv_t = _np.rint(_np.linalg.inv(rot))",
                      "the coefficient map uses R^-T");
        checkContains(script, "_np.exp(-2j * _np.pi * ((gp + kfrac) @ tau))",
                      "with the exp(-2 pi i (k+G').t) phase");
        // Projectivity is decided from the FACTOR SYSTEM — a property of the
        // group and k — not inferred from how nearly integral a reduction came
        // out, which would conflate it with an unconverged empty band.
        checkContains(script, "def _calango_projective(",
                      "a nonsymmorphic zone boundary is reported, not labelled");
        checkContains(script, "'projective': bool(_sym_point_projective)",
                      "from the factor system rather than from the residual");
        checkContains(script, "'resolved': _sym_resolved",
                      "and an unreducible multiplet withholds its label");
        checkContains(script, "get_pseudo_wave_function",
                      "the states themselves are read");
        // Line classification is what makes the compatibility relations
        // readable, and it is switchable.
        checkContains(script, "'line'", "symmetry lines are classified too");
        ElectronicConfig pointsOnly = config;
        pointsOnly.symmetry.classifyLines = false;
        check(!contains(generateElectronicScript(pointsOnly), "    if True:\n"),
              "and can be turned off");

        // Tolerances reach the script rather than being hardcoded.
        ElectronicConfig tuned = config;
        tuned.symmetry.symprec = 0.002;
        tuned.symmetry.degeneracyEv = 0.005;
        const std::string tunedScript = generateElectronicScript(tuned);
        checkContains(tunedScript, "_sym_symprec = 0.002",
                      "the symmetry tolerance is the wizard's");
        checkContains(tunedScript, "_sym_degen = 0.005",
                      "and so is the degeneracy window");

        // Spin-orbit coupling makes the states two-component SPINORS, which a
        // point group cannot represent at all: a spinor changes sign under a
        // 2 pi rotation. The classification is not suppressed, it switches to
        // the DOUBLE group — and picking the wrong one of the two is not a
        // coarser answer but a wrong one, so which group is used has to follow
        // the states rather than a preference.
        ElectronicConfig soc = config;
        soc.spinOrbit = true;
        const std::string socScript = generateElectronicScript(soc);
        checkContains(socScript, "band_symmetry.json",
                      "SOC still writes a classification");
        checkContains(socScript, "_sym_spinor = True",
                      "and marks the states as spinors");
        checkContains(socScript, "_calango_double_group(",
                      "which routes it through the double group");
        checkContains(socScript, "_calango_su2(",
                      "with the SU(2) lift the spinor characters need");
        checkContains(socScript, "wavefunctions(",
                      "reading the spinor wave functions, not the scalar ones");
        // The scalar path must be untouched: without SOC the single group is
        // still the right one, and paying for the double group there would
        // relabel every existing result.
        check(contains(script, "_sym_spinor = False"),
              "a scalar run still uses the ordinary point group");

        // Fatbands genuinely cannot follow SOC — the weights belong to the
        // scalar states — so that one IS still refused, and says so.
        ElectronicConfig socFat = soc;
        socFat.fatbands = true;
        const std::string socFatScript = generateElectronicScript(socFat);
        checkContains(socFatScript, "orbital projections were skipped",
                      "SOC still refuses fatbands, loudly");
        check(!contains(socFatScript, "fatbands.json"),
              "and writes no fatband file for them");
    }

    // -- Orbital-projected bands (fatbands) ---------------------------------
    std::printf("Orbital-projected bands (fatbands):\n");
    {
        ElectronicConfig config;
        config.backend = ElectronicBackend::Gpaw;
        config.baselineDensityPath = "/jobs/proc_1/single_point.gpw";
        check(!contains(generateElectronicScript(config), "fatbands.json"),
              "no projections unless they were asked for");

        config.fatbands = true;
        const std::string derived = generateElectronicScript(config);
        checkContains(derived, "fatbands.json", "the result file is written");
        // pdos_weights is the portable entry point across BOTH GPAW engines;
        // get_orbital_ldos is gone from the new one, which is the drift that
        // silently emptied the PDOS once already.
        checkContains(derived, "from gpaw.dos import DOSCalculator as _FatDOS",
                      "the projections go through gpaw.dos");
        checkContains(derived, "pdos_weights(",
                      "band- and k-resolved, not energy-integrated");
        checkContains(derived, "get_projector_numbers",
                      "with the shell's projector indices looked up per setup");
        checkContains(derived, "for _fat_sym in sorted(set(_fat_symbols)):",
                      "an empty selection derives one channel per element");
        // (nkpt, nband, nspin) from GPAW against (nspin, nkpt, nband) in the
        // band energies: a transpose that is invisible until the weights are
        // drawn on the wrong bands.
        checkContains(derived, "transpose(2, 0, 1)",
                      "the weight array is matched to the energy array");

        ElectronicConfig explicitChannels = config;
        FatbandProjection pz;
        pz.label = "C p_z";
        pz.atoms = {0, 1};
        pz.angularMomentum = 1;
        pz.magnetic = 1;
        explicitChannels.fatbandProjections = {pz};
        const std::string chosen = generateElectronicScript(explicitChannels);
        checkContains(chosen, "'label': \"C p_z\", 'atoms': [0, 1]",
                      "an explicit atom selection is baked in");
        checkContains(chosen, "'l': 1, 'm': 1",
                      "together with the shell and magnetic sub-level");
        checkContains(chosen, "_fat_ind[_fat_ch['m']::2 * _fat_ch['l'] + 1]",
                      "and one m is strided out of the shell");
        check(!contains(chosen, "for _fat_sym in sorted(set(_fat_symbols)):"),
              "without also deriving the per-element defaults");

        ElectronicConfig soc = config;
        soc.spinOrbit = true;
        check(!contains(generateElectronicScript(soc), "fatbands.json"),
              "SOC suppresses the projections for the same reason");
    }

    std::printf("VASP electronic structure:\n");
    {
        ElectronicConfig bands;
        bands.backend = ElectronicBackend::Vasp;
        bands.kpath = "GXWKG";

        // No baseline: the script has to converge its own density first, and
        // must write it (LCHARG) or the ICHARG = 11 pass below has nothing to
        // read.
        const std::string fresh = generateElectronicScript(bands);
        checkContains(fresh, "lcharg=True",
                      "a self-contained run writes the density it will reuse");
        checkContains(fresh, "icharg=11",
                      "and the band pass is non-self-consistent");

        // With a baseline: no SCF at all.
        ElectronicConfig nscf = bands;
        nscf.baselineDensityPath = "/jobs/proc_1/CHGCAR";
        const std::string reused = generateElectronicScript(nscf);
        checkContains(reused, "icharg=11",
                      "a reused density is read with ICHARG = 11");
        checkContains(reused, "shutil.copyfile(_baseline, 'CHGCAR')",
                      "and copied in, since VASP takes no path for it");
        check(!contains(reused, "scf = Vasp("),
              "with NO SCF calculator built — that is the whole point");
        checkContains(reused, "The baseline charge density is gone",
                      "a missing baseline is caught before VASP starts");
    }

    std::printf("XAS generation:\n");
    {
        XasRunConfig xas;
        xas.element = "O";
        xas.absorbingAtom = 2;
        xas.calculator.calculator = CalculatorKind::Gpaw;
        xas.calculator.gpawMode = GpawMode::FiniteDifference;
        const std::string script =
            XasScriptGenerator::generate(xas, "structure.extxyz");

        // The one thing that is not obvious from the tutorial and kills the
        // run outright: gpaw.xas refuses to work on the new engine, which
        // every other script Calango emits turns ON.
        checkContains(script, "os.environ['GPAW_NEW'] = '0'",
                      "the new GPAW engine is disabled");
        checkContains(script, "legacy_gpaw=True",
                      "and the legacy path asked for by name");

        // Core-hole setup generation, keyed to the level and the occupation.
        checkContains(script, "from gpaw.atom.generator import Generator",
                      "the core-hole setup is generated");
        checkContains(script, "corehole=(1, 0, 0.5)",
                      "a K-edge half hole is (n=1, l=0, 0.5)");
        checkContains(script, "setup_paths.insert(0, '.')",
                      "and found from the job directory, not installed");
        // Applied to the ATOM, not the element: keying by element would put a
        // core hole on every atom of that species at once.
        checkContains(script, "setups={absorbing_atom: setup_name}",
                      "the setup goes on the absorbing atom by index");
        checkContains(script, "absorbing_atom = 2",
                      "which is the one the user chose");
        checkContains(script, "nbands=-30",
                      "unoccupied bands are requested as a negative count");
        checkContains(script, "from gpaw.xas import XAS",
                      "the spectrum comes from gpaw.xas");
        checkContains(script, "xas.json", "and lands in the results file");

        // The other edges and hole occupations.
        XasRunConfig l23 = xas;
        l23.coreLevel = XasCoreLevel::L23;
        checkContains(XasScriptGenerator::generate(l23, "s.extxyz"),
                      "corehole=(2, 1, 0.5)", "an L2,3 edge is (n=2, l=1)");
        XasRunConfig full = xas;
        full.coreHole = XasCoreHole::Full;
        const std::string fullScript =
            XasScriptGenerator::generate(full, "s.extxyz");
        checkContains(fullScript, "corehole=(1, 0, 1)",
                      "a full hole removes one electron");
        checkContains(fullScript, "setup_name = \"fch1s\"",
                      "and is named for what it is");
        XasRunConfig none = xas;
        none.coreHole = XasCoreHole::None;
        checkContains(XasScriptGenerator::generate(none, "s.extxyz"),
                      "corehole=(1, 0, 0)", "no hole removes nothing");

        // Delta-Kohn-Sham calibration is opt-in and costs two more runs.
        check(!contains(script, "get_reference_energy"),
              "no delta-Kohn-Sham calculation unless asked for");
        XasRunConfig dks = xas;
        dks.computeDks = true;
        const std::string dksScript =
            XasScriptGenerator::generate(dks, "s.extxyz");
        checkContains(dksScript, "get_reference_energy",
                      "asking for it adds the two total energies");
        checkContains(dksScript, "fixmagmom=True",
                      "with the moment fixed so the excited state survives");
        checkContains(dksScript, "charge=-1",
                      "and the cell kept neutral");

        // The absorbing atom has to BE the element the setup was made for.
        checkContains(script, "is {_actual}, not {element}",
                      "a mismatched atom and element is caught in the script");
    }

    std::printf("VASP relaxation driver:\n");
    {
        // The bug this pins: VASP relaxes internally when IBRION/NSW say so,
        // and ASE's optimizer relaxes anything that returns forces. With both
        // emitted, every ASE force evaluation ran a complete VASP relaxation.
        // Exactly one of them must take the ionic steps.
        CalculatorConfig relax;
        relax.calculator = CalculatorKind::Vasp;
        relax.task = TaskKind::GeometryOptimization;
        relax.maxSteps = 80;
        relax.vaspPotcarPath = "/opt/vasp/POTCARs";

        // Default: ASE drives, so VASP must be static.
        relax.vaspRelaxDriver = VaspRelaxDriver::Ase;
        const std::string ase =
            AseScriptGenerator::generate(relax, "structure.extxyz");
        checkContains(ase, "from ase.optimize import BFGS",
                      "ASE-driven: the optimizer is imported");
        checkContains(ase, "opt.run(fmax=",
                      "and it is the thing that runs");
        checkContains(ase, "ibrion=-1",
                      "while VASP is pinned to a static calculation");
        checkContains(ase, "nsw=0", "with no ionic steps of its own");
        check(!contains(ase, "ediffg="),
              "and no ionic convergence criterion, which would be VASP's");

        // VASP-driven: no ASE optimizer at all.
        relax.vaspRelaxDriver = VaspRelaxDriver::Vasp;
        const std::string internal =
            AseScriptGenerator::generate(relax, "structure.extxyz");
        checkContains(internal, "ibrion=2", "VASP-driven: IBRION is written");
        checkContains(internal, "nsw=80", "with the step budget as NSW");
        checkContains(internal, "ediffg=", "and its own force criterion");
        check(!contains(internal, "from ase.optimize import"),
              "and NO ASE optimizer is imported");
        check(!contains(internal, "opt.run("),
              "nor run — this is the whole fix");
        checkContains(internal, "geometry_optimization.json",
                      "the viewer's summary is still written");
        checkContains(internal, "opt.traj",
                      "and the ionic path, recovered from OUTCAR");

        // A single point is static under either setting: there is nothing to
        // drive, and NSW > 0 on a single point would silently relax it.
        for (const VaspRelaxDriver driver :
             {VaspRelaxDriver::Ase, VaspRelaxDriver::Vasp}) {
            CalculatorConfig single = relax;
            single.task = TaskKind::SinglePoint;
            single.vaspRelaxDriver = driver;
            const std::string script =
                AseScriptGenerator::generate(single, "s.extxyz");
            checkContains(script, "nsw=0",
                          "a single point never takes ionic steps");
        }

        // Non-VASP engines are untouched by the driver setting.
        CalculatorConfig gpawRelax = relax;
        gpawRelax.calculator = CalculatorKind::Gpaw;
        gpawRelax.vaspRelaxDriver = VaspRelaxDriver::Vasp;
        checkContains(AseScriptGenerator::generate(gpawRelax, "s.extxyz"),
                      "from ase.optimize import",
                      "the driver choice does not leak into other engines");
    }

    std::printf("GPAW density exports:\n");
    {
        CalculatorConfig config = gpawConfig();
        config.gpawExportDensity = true;
        config.gpawDensityExports = {true, true, true, true, true, true};
        const std::string script =
            AseScriptGenerator::generate(config, "structure.extxyz");
        checkContains(script, "density_all_electron.cube", "all-electron density");
        checkContains(script, "density_pseudo.cube", "pseudodensity");
        checkContains(script, "density_spin.cube", "spin density");
        checkContains(script, "potential_hartree.cube", "Hartree potential");
        checkContains(script, "elf.cube", "ELF");
        checkContains(script, "kinetic_energy_density.cube",
                      "kinetic energy density");
        // The two whose API is not obvious, pinned by name: both were
        // established against a live GPAW 26.x, and both moved between
        // releases.
        checkContains(script, "elf_from_dft_calculation",
                      "ELF uses the module-level entry point");
        checkContains(script, "density.update_ked(dft.ibzwfs)",
                      "tau is built before it is read");
        checkContains(script, "density.taut_sR",
                      "and read off the density object");
        // One unsupported field must not cost the others, which the converged
        // SCF has already paid for.
        checkContains(script, "CALANGO_INFO", "a failing field is reported");

        // Only what was asked for.
        CalculatorConfig one = gpawConfig();
        one.gpawExportDensity = true;
        one.gpawDensityExports.elf = true;
        const std::string onlyElf =
            AseScriptGenerator::generate(one, "structure.extxyz");
        checkContains(onlyElf, "elf.cube", "a single selection emits its field");
        check(!contains(onlyElf, "potential_hartree.cube"),
              "and nothing else");

        // A config from a saved project carries only the old single choice.
        CalculatorConfig legacy = gpawConfig();
        legacy.gpawExportDensity = true;
        legacy.gpawDensityType = GpawDensityType::AllElectron;
        const std::string legacyScript =
            AseScriptGenerator::generate(legacy, "structure.extxyz");
        checkContains(legacyScript, "density_all_electron.cube",
                      "an empty selection falls back to the legacy density type");

        // The file names are a contract with the GUI, which maps each one to a
        // display label in the Volumetric Data dock. They drifted once — the
        // dock looked for "hartree_potential.cube" while the script wrote
        // "potential_hartree.cube" — so the constants both ends now share are
        // asserted against the emitted text here.
        checkContains(script, densityFiles::kAllElectron, "shared constant: all-electron");
        checkContains(script, densityFiles::kPseudo, "shared constant: pseudo");
        checkContains(script, densityFiles::kSpin, "shared constant: spin");
        checkContains(script, densityFiles::kHartree, "shared constant: Hartree");
        checkContains(script, densityFiles::kElf, "shared constant: ELF");
        checkContains(script, densityFiles::kKineticEnergy,
                      "shared constant: kinetic energy density");
        checkContains(AseScriptGenerator::densityCubeScript("/jobs/proc_1", false),
                      densityFiles::kDensity, "shared constant: plain density");
    }

    // -- Occupation smearing -------------------------------------------------
    // Every method must emit its OWN GPAW occupation function with only the
    // keys that method accepts. They all used to collapse onto Fermi-Dirac,
    // which silently ran a different physical model than the one selected.
    std::printf("GPAW occupation smearing:\n");
    {
        const auto snippet = [](SmearingMethod method, double width = 0.15,
                                int order = 2) {
            CalculatorConfig c = gpawConfig();
            c.smearing = method;
            c.smearingWidthEv = width;
            c.smearingOrder = order;
            return AseScriptGenerator::calculatorSnippet(c);
        };

        checkContains(snippet(SmearingMethod::FermiDirac),
                      "occupations={\"name\": \"fermi-dirac\", \"width\": 0.15}",
                      "Fermi-Dirac carries only a width");

        // Gaussian IS Methfessel-Paxton at order 0 — that is its definition,
        // and GPAW has no separate name for it.
        const std::string gaussian = snippet(SmearingMethod::Gaussian);
        checkContains(gaussian, "\"name\": \"methfessel-paxton\"",
                      "Gaussian resolves to methfessel-paxton");
        checkContains(gaussian, "\"order\": 0", "at order 0");

        const std::string mp = snippet(SmearingMethod::MethfesselPaxton);
        checkContains(mp, "\"name\": \"methfessel-paxton\"", "MP by name");
        checkContains(mp, "\"width\": 0.15", "MP carries the width");
        checkContains(mp, "\"order\": 2", "and the user's expansion order");

        checkContains(snippet(SmearingMethod::MarzariVanderbilt),
                      "\"name\": \"marzari-vanderbilt\", \"width\": 0.15",
                      "Marzari-Vanderbilt cold smearing");

        // The exact BZ integrators take no width at all, and GPAW raises on an
        // unexpected key rather than ignoring it.
        for (const auto& [method, name] :
             {std::pair{SmearingMethod::TetrahedronMethod, "tetrahedron-method"},
              std::pair{SmearingMethod::ImprovedTetrahedronMethod,
                        "improved-tetrahedron-method"},
              std::pair{SmearingMethod::OrbitalFree, "orbital-free"}}) {
            const std::string script = snippet(method);
            checkContains(script, std::string("{\"name\": \"") + name + "\"}",
                          std::string(name) + " takes no parameters");
            check(!contains(script, std::string("\"") + name + "\", \"width\""),
                  std::string(name) + " emits no width");
        }

        CalculatorConfig fixed = gpawConfig();
        fixed.smearing = SmearingMethod::FixedOccupations;
        fixed.fixedOccupations = {{1, 0, 1, 0}, {1, 1, 0, 0}};
        checkContains(AseScriptGenerator::calculatorSnippet(fixed),
                      "{\"name\": \"fixed\", \"numbers\": [[1, 0, 1, 0], "
                      "[1, 1, 0, 0]]}",
                      "fixed occupations emit one list per spin channel");

        // No occupation numbers is not something the generator can guess a
        // value for, so it marks the gap instead of emitting a plausible run.
        CalculatorConfig empty = gpawConfig();
        empty.smearing = SmearingMethod::FixedOccupations;
        checkContains(AseScriptGenerator::calculatorSnippet(empty), "<- EMPTY",
                      "an empty occupation list is flagged in the script");

        checkContains(snippet(SmearingMethod::None),
                      "occupations={\"name\": \"fermi-dirac\", \"width\": 0.0}",
                      "None is zero-width Fermi-Dirac");

        // VASP encodes method and order in one integer. The tetrahedron
        // schemes have real ISMEAR equivalents; the rest do not, and the
        // script has to say which run it is actually about to perform.
        const auto vasp = [](SmearingMethod method, int order = 1) {
            CalculatorConfig c;
            c.calculator = CalculatorKind::Vasp;
            c.task = TaskKind::SinglePoint;
            c.smearing = method;
            c.smearingOrder = order;
            return AseScriptGenerator::calculatorSnippet(c);
        };
        checkContains(vasp(SmearingMethod::FermiDirac), "ismear=-1",
                      "ISMEAR -1 is Fermi-Dirac");
        checkContains(vasp(SmearingMethod::Gaussian), "ismear=0",
                      "ISMEAR 0 is Gaussian");
        checkContains(vasp(SmearingMethod::MethfesselPaxton, 3), "ismear=3",
                      "ISMEAR N is the MP order itself");
        checkContains(vasp(SmearingMethod::TetrahedronMethod), "ismear=-4",
                      "ISMEAR -4 is the linear tetrahedron method");
        checkContains(vasp(SmearingMethod::ImprovedTetrahedronMethod),
                      "ismear=-5", "ISMEAR -5 adds the Blochl correction");
        checkContains(vasp(SmearingMethod::MarzariVanderbilt),
                      "no VASP ISMEAR equivalent",
                      "an approximated method says so in the script");
    }

    // -- Initial magnetic moments -------------------------------------------
    // Whether the structure CARRIES an initial_magmoms column and whether the
    // values in it are non-zero are different questions. Conflating them
    // overwrote a deliberate all-zero seed with the uniform fallback, so a
    // user who set every moment to 0 in Edit Structure got 1 uB per atom.
    std::printf("Initial magnetic moments:\n");
    {
        CalculatorConfig spin = gpawConfig();
        spin.spinMode = SpinMode::Collinear;
        spin.spinPolarized = true;
        spin.initialMagMoment = 1.0;
        const std::string script =
            AseScriptGenerator::generate(spin, "structure.extxyz");
        checkContains(script, "if atoms.has('initial_magmoms'):",
                      "the seed is chosen on the column's PRESENCE");
        check(!contains(script, "if _np.abs(_seeded).max(initial=0.0) > 1e-12:"),
              "not on whether its values happen to be non-zero");
        // The fallback must still exist for a structure that carries nothing.
        checkContains(script, "atoms.set_initial_magnetic_moments(",
                      "a structure with no moments still gets the fallback");
        checkContains(script, "CALANGO_WARN the structure carries no initial",
                      "and says so");
        // An all-zero column is a legitimate, reported choice.
        checkContains(script, "every initial magnetic moment is",
                      "an all-zero seed is reported as deliberate");
    }
    {
        // The converged moments have to leave the run, or the viewer and the
        // viewport have nothing but the input guess to show.
        CalculatorConfig spin = gpawConfig();
        spin.spinMode = SpinMode::Collinear;
        const std::string script =
            AseScriptGenerator::generate(spin, "structure.extxyz");
        checkContains(script, "atoms.get_magnetic_moments()",
                      "per-atom moments are read back");
        checkContains(script, "\"magnetic_moments\": _magmoms",
                      "and written into the summary");
        checkContains(script, "write(\"single_point.extxyz\", atoms)",
                      "a result structure carries them to the viewport");
    }

    // -- 2D band surfaces ----------------------------------------------------
    std::printf("2D band surfaces:\n");
    {
        TwoDBandsConfig cfg;
        cfg.baselineDensityPath = "/jobs/proc_1/single_point.gpw";
        cfg.gridSamples = 32;
        const std::string script = generateTwoDBandsScript(cfg);
        checkContains(script, "/jobs/proc_1/single_point.gpw",
                      "restarts from the selected baseline density");
        checkContains(script, "_n = 32", "honors the requested grid");
        checkContains(script, "np.linspace(-0.5, 0.5, _n)",
                      "Gamma-centred and inclusive of both zone edges");
        checkContains(script, "symmetry='off'",
                      "keeps every grid point rather than an irreducible wedge");
        // kz is never written, so it stays 0: the 2D zone is the kz = 0 plane.
        checkContains(script, "_kpts[:, 1] = _fy.ravel()",
                      "samples kx and ky only");
        check(!contains(script, "_kpts[:, 2] ="), "leaving kz at zero");
        checkContains(script, "2.0 * np.pi * np.asarray(atoms.cell.reciprocal())",
                      "exports Cartesian k in 1/A with the 2pi restored");
        checkContains(script, "if not (_pbc[0] and _pbc[1]):",
                      "refuses a structure with no 2D periodicity");
        checkContains(script, "CALANGO_RESULT bands_2d=bands_2d.json",
                      "emits the marker the controller watches for");
        // bandpath() IS called — but only to read the lattice's special-point
        // labels. What must not happen is sampling along it: the surface is
        // the object, not a set of cuts through it.
        checkContains(script, "bandpath(npoints=0)",
                      "the k-path is consulted for labels only");
        check(!contains(script, "kpts=bandpath")
                  && !contains(script, "fixed_density(kpts=bandpath"),
              "and never used as the sampling");
        checkContains(script, "band_calc = calc.fixed_density(kpts=_kpts",
                      "the run samples the explicit 2D grid instead");

        TwoDBandsConfig soc = cfg;
        soc.spinOrbit = true;
        checkContains(generateTwoDBandsScript(soc), "soc_eigenstates",
                      "spin-orbit re-diagonalizes in the spinor basis");

        // N <= 2 cannot be triangulated into a surface; the generator clamps
        // rather than emitting a grid the viewer would silently drop.
        TwoDBandsConfig tiny = cfg;
        tiny.gridSamples = 1;
        checkContains(generateTwoDBandsScript(tiny), "_n = 3",
                      "a degenerate grid request is clamped");

        // The Brillouin-zone map is strictly opt-in. "No bz_map key at all"
        // is the compatibility contract: the results viewer greys its map
        // entry on the key's absence, and an old run must stay byte-identical
        // to what this generator has always produced.
        check(!contains(script, "bz_map"),
              "a run without the option carries no bz_map at all");

        TwoDBandsConfig mapped = cfg;
        mapped.bzMap = true;
        mapped.bzMapSamples = 18;
        const std::string mapScript = generateTwoDBandsScript(mapped);
        checkContains(mapScript, "_bzn = 18",
                      "the map honors its own mesh size");
        checkContains(
            mapScript,
            "(2.0 * np.arange(1, _bzn + 1) - _bzn - 1) / (2.0 * _bzn)",
            "a Monkhorst-Pack line, tiling the cell with no duplicated seam");
        checkContains(mapScript, "_bz_calc = calc.fixed_density(kpts=_bz_kpts",
                      "diagonalized at fixed density like the surfaces");
        check(!contains(mapScript, "_bz_kpts[:, 2] ="),
              "with kz left at zero — the vacuum axis stays excluded");
        checkContains(mapScript, "result['bz_map'] = {",
                      "appended to the same bands_2d.json, not a second file");
        checkContains(mapScript, "'kpts_frac':", "carrying the fractional mesh");
        checkContains(mapScript, "'efermi_eV': float(efermi)",
                      "the Fermi level");
        checkContains(mapScript, "'reciprocal_A_inv':",
                      "and the in-plane reciprocal rows the fold is built from");
        // The map view picks a band by INDEX, so "band n" must mean exactly
        // one thing at every k — the two spin channels are merged and sorted.
        checkContains(mapScript, "np.sort(np.concatenate(",
                      "spin channels are merged and sorted per k-point");

        TwoDBandsConfig mappedSoc = mapped;
        mappedSoc.spinOrbit = true;
        checkContains(generateTwoDBandsScript(mappedSoc),
                      "soc_eigenstates(_bz_calc)",
                      "a spin-orbit run maps the same spinor bands it plots");

        // The map mesh has its own clamp (6..96): its cost is N² extra
        // fixed-density diagonalizations on top of the surface grid.
        TwoDBandsConfig hugeMap = mapped;
        hugeMap.bzMapSamples = 4096;
        checkContains(generateTwoDBandsScript(hugeMap), "_bzn = 96",
                      "a runaway map mesh is clamped");
    }

    // -- MLWF -> Wannier interpolation handoff --------------------------------
    // The reported crash: an MLWF started from a single-point baseline reads
    // that baseline's .gpw and writes none of its own, so the interpolation's
    // glob of the MLWF directory found nothing and it died on line 11.
    std::printf("Wannier interpolation handoff:\n");
    {
        WannierConfig baseline;
        baseline.baselineDir = "/jobs/proc_100";
        const std::string mlwf = generateWannierScript(baseline);
        checkContains(mlwf, "_gpw_path = os.path.abspath(_gpw[0])",
                      "an MLWF from a baseline records where the .gpw actually is");
        checkContains(mlwf, "'gpw': _gpw_path",
                      "and writes that path into wannier.json");
        checkContains(mlwf, "'nwannier': int(nwannier)",
                      "along with the count the interpolation must reproduce");

        WannierConfig fresh; // no baseline: runs its own SCF
        fresh.calculator.calculator = CalculatorKind::Gpaw;
        const std::string own = generateWannierScript(fresh);
        checkContains(own, "calc.write('wannier.gpw', mode='all')",
                      "a fresh SCF writes its own wavefunctions");
        checkContains(own, "_gpw_path = os.path.abspath('wannier.gpw')",
                      "and records that path too");

        WannierInterpolationConfig interp;
        const std::string script =
            generateWannierInterpolationScript("/jobs/proc_101", interp);
        checkContains(script, "_gpw_path = _meta.get('gpw')",
                      "the interpolation reads the recorded path first");
        checkContains(script, "glob.glob(os.path.join(_base, '*.gpw'))",
                      "and falls back to the MLWF directory");
        checkContains(script, "Re-run the MLWF calculation",
                      "the failure names the fix rather than the directory");
        checkContains(script, "len(calc.get_ibz_k_points()) < "
                              "len(calc.get_bz_k_points())",
                      "a symmetry-reduced .gpw is caught before Wannier chokes");
        checkContains(script, "_meta.get('nwannier')",
                      "the recorded Wannier count is preferred over len(centers)");
    }

    // -- MLWF fixed states: exactly one of ASE's two selectors ---------------
    //
    // ase.dft.wannier.choose_states() raises RuntimeError('You can not set
    // both fixedenergy and fixedstates'), so the two are one choice. Verified
    // against ase 3.29.0: the keywords, their mutual exclusivity and the
    // reference level below are all read off that implementation.
    std::printf("MLWF fixed-state selection:\n");
    {
        WannierConfig def; // FromWannierCount
        const std::string plain = generateWannierScript(def);
        checkContains(plain, "_fixedenergy = None",
                      "the default passes no energy window");
        checkContains(plain, "_fixedstates = None",
                      "and no explicit band count");

        WannierConfig window;
        window.fixedMode = WannierConfig::FixedStatesMode::EnergyWindow;
        window.energyWindowEv = 2.5;
        const std::string byEnergy = generateWannierScript(window);
        checkContains(byEnergy, "_fixedenergy = 2.5",
                      "the energy window reaches ASE as fixedenergy");
        checkContains(byEnergy, "_fixedstates = None",
                      "and leaves fixedstates unset — ASE refuses both");
        // The reference level is ASE's, and it is NOT the Fermi level for a
        // gapped system. The script has to say so, because the number alone
        // reads as "above E_F".
        checkContains(byEnergy, "CONDUCTION BAND MINIMUM",
                      "the script states the real reference level");

        WannierConfig bands;
        bands.fixedMode = WannierConfig::FixedStatesMode::BandCount;
        bands.fixedStates = 6;
        const std::string byCount = generateWannierScript(bands);
        checkContains(byCount, "_fixedstates = 6",
                      "an explicit band count reaches ASE as fixedstates");
        checkContains(byCount, "_fixedenergy = None",
                      "and leaves fixedenergy unset");

        // Both keywords are always passed by name, so the call is valid for
        // all three modes without the generator splicing kwargs together.
        checkContains(byCount,
                      "fixedenergy=_fixedenergy, fixedstates=_fixedstates",
                      "one call form covers every mode");

        // edf_k = nwannier - fixedstates_k, unchecked by ASE: fixing more
        // states than there are Wannier functions dies in the rotation setup.
        checkContains(plain, "ASE needs at least as",
                      "the run explains a negative extra-degrees-of-freedom "
                      "count instead of failing opaquely");
    }

    // -- Interpolation windows actually reach ASE ----------------------------
    //
    // The inner/outer windows used to be emitted as a COMMENT: the dialog
    // offered them, the script recorded them, and the calculation ignored
    // them. Inner is fixedenergy; outer is nbands ("bands to include in
    // localization"), resolved from the eigenvalues at run time.
    std::printf("Wannier interpolation windows:\n");
    {
        WannierInterpolationConfig off;
        const std::string none = generateWannierInterpolationScript("/j", off);
        checkContains(none, "_fixedenergy = None",
                      "no inner window means no fixedenergy");
        checkContains(none, "_nbands = None",
                      "no outer window means every band is available");

        WannierInterpolationConfig on;
        on.useInnerWindow = true;
        on.innerWindowEv = 1.5;
        on.useOuterWindow = true;
        on.outerWindowEv = 8.0;
        const std::string windows = generateWannierInterpolationScript("/j", on);
        checkContains(windows, "_fixedenergy = 1.5",
                      "the inner window is passed as fixedenergy");
        checkContains(windows, "_outer = 8",
                      "the outer window is carried into the script");
        checkContains(windows, "fixedenergy=_fixedenergy, nbands=_nbands",
                      "and both are handed to Wannier, not written in a "
                      "comment");
        checkContains(windows, "calc.get_eigenvalues",
                      "the outer cutoff becomes a band count from the real "
                      "eigenvalues");
        checkContains(windows, "_nbands = max(_nbands, nwannier)",
                      "clamped so the pool still spans the manifold");
        check(!contains(windows, "ASE's Wannier disentanglement is limited"),
              "the old \"recorded but not applied\" disclaimer is gone");
    }

    // -- Optics parameter passing --------------------------------------------
    // The reported symptom was that changing the energy-point count or the
    // k-mesh produced identical spectra. The cause: the wizard collected
    // npoints / omega bounds and the generator emitted NONE of them, leaving
    // GPAW to build its own default frequency grid, which was then read back.
    std::printf("Optics parameter passing:\n");
    {
        const auto optics = [](int npoints, double omegaMin, double omegaMax,
                               int kx, int ky, int kz) {
            OpticsConfig c;
            c.baselineDensityPath = "/jobs/proc_1/single_point.gpw";
            c.npoints = npoints;
            c.omegaMinEv = omegaMin;
            c.omegaMaxEv = omegaMax;
            c.responseKpts[0] = kx;
            c.responseKpts[1] = ky;
            c.responseKpts[2] = kz;
            return generateOpticsScript(c);
        };

        const std::string base = optics(500, 0.0, 20.0, 0, 0, 0);
        checkContains(base, "np.linspace(0, 20, 500)",
                      "the requested frequency grid reaches the script");
        checkContains(base, "frequencies=_frequency_arg",
                      "and is handed to DielectricFunction");
        checkContains(base, "_frequency_arg = frequencies_eV",
                      "which for point integration IS the requested grid");
        checkContains(base, "_hilbert = False",
                      "with the transform off, as an explicit grid requires");

        // The actual regression: two different requests must not produce the
        // same script.
        check(optics(500, 0.0, 20.0, 0, 0, 0)
                  != optics(2000, 0.0, 20.0, 0, 0, 0),
              "changing the point count changes the script");
        check(optics(500, 0.0, 20.0, 0, 0, 0)
                  != optics(500, 0.0, 40.0, 0, 0, 0),
              "changing the energy window changes the script");
        check(optics(500, 0.0, 20.0, 0, 0, 0)
                  != optics(500, 0.0, 20.0, 24, 24, 1),
              "changing the k-mesh changes the script");

        // Per-axis "auto". A 2D sheet is naturally entered as 24, 24, auto —
        // which used to discard the WHOLE mesh, because the emitter required
        // all three axes to be non-zero.
        const std::string partial = optics(500, 0.0, 20.0, 24, 24, 0);
        checkContains(partial, "kpts=_response_kpts",
                      "a partially-specified mesh is still applied");
        checkContains(partial, "_requested_kpts = (24, 24, 0)",
                      "with the zero axis carried through as \"inherit\"");
        checkContains(partial, "_baseline_kpts[_i]",
                      "and resolved against the baseline's own grid");
        check(optics(500, 0.0, 20.0, 24, 24, 1)
                  != optics(500, 0.0, 20.0, 24, 24, 0),
              "an explicit z divisions differs from inheriting it");

        // The sampling has to be recorded, or two runs are indistinguishable
        // from their own output files.
        checkContains(base, "\"response_kpts\": list(_response_kpts)",
                      "the mesh actually used is written into optics.json");
        checkContains(base, "\"npoints\": int(len(omega_eV))",
                      "along with the grid density");
    }

    // -- Raman / IR without Born charges -------------------------------------
    // Z* is the only route to an IR intensity in a periodic crystal, but the
    // phonons and the Raman spectrum do not depend on it — so a missing Born
    // charges run must cost the IR column, not the job.
    std::printf("Raman / IR without Born charges:\n");
    {
        RamanIrConfig cfg;
        cfg.calculator.calculator = CalculatorKind::Gpaw;
        cfg.baselinePath = "/jobs/proc_1/single_point.gpw";
        const std::string without = generateRamanIrScript(cfg);
        checkContains(without, "BORN_CHARGES = None",
                      "no Born charges selected is representable");
        checkContains(without, "if BORN_CHARGES is not None:",
                      "and the IR block becomes conditional");
        check(!contains(without,
                        "'CALANGO_ERROR the IR intensities need Born effective"),
              "the run is no longer refused for want of Z*");
        checkContains(without, "'ir': ir_meta",
                      "raman_ir.json records whether IR was computed");
        checkContains(without, "CALANGO_WARN no Born effective charges",
                      "and the omission is reported rather than silent");

        cfg.bornChargesPath = "/jobs/proc_2/born_charges.json";
        const std::string with = generateRamanIrScript(cfg);
        checkContains(with, "/jobs/proc_2/born_charges.json",
                      "a supplied Z* set still reaches the script");
        // A PARTIAL Z* set stays fatal: there the user did ask for IR, and a
        // silently zeroed atom gives a plausible spectrum with wrong
        // intensities.
        checkContains(with, "CALANGO_ERROR the Born charges run covered only",
                      "a partial Z* set is still refused");
    }

    // -- Raman / IR on VASP and Quantum ESPRESSO -----------------------------
    //
    // Three engines, one physics core. What is pinned here is (a) that each
    // engine really takes its own route to the Hessian and Z* rather than
    // falling back to the GPAW displacement loop, and (b) that all three still
    // feed the SAME shared contractions and the SAME raman_ir.json — the whole
    // reason the viewer and the phonon LO-TO block have one reader each.
    std::printf("Raman / IR on VASP and Quantum ESPRESSO:\n");
    {
        RamanIrConfig base;
        base.calculator.planeWaveCutoffEv = 520.0;
        base.calculator.kpts[0] = 6;
        base.calculator.kpts[1] = 6;
        base.calculator.kpts[2] = 6;

        RamanIrConfig vaspCfg = base;
        vaspCfg.calculator.calculator = CalculatorKind::Vasp;
        vaspCfg.calculator.vaspXc = "PBEsol";
        const std::string vasp = generateRamanIrScript(vaspCfg);

        // DFPT, not 6N displaced force runs: one linear-response job returns
        // the force constants AND every Z*.
        checkContains(vasp, "ibrion=8", "VASP takes the DFPT route");
        checkContains(vasp, "lepsilon=True",
                      "with the same run returning Z* and eps_inf");
        checkContains(vasp, "nwrite=3",
                      "NWRITE=3 is what makes VASP print the Hessian at all");
        checkContains(vasp, "ediff=1e-8",
                      "linear response needs a far tighter SCF than an energy");
        checkContains(vasp, "lreal=False",
                      "LREAL=Auto is not supported alongside LEPSILON");
        checkContains(vasp, "SECOND DERIVATIVES (NOT SYMMETRIZED)",
                      "the force constants are read from the OUTCAR table");
        checkContains(vasp, "return -np.asarray(rows, dtype=float)",
                      "VASP prints dF/du, so the parsed matrix is negated");
        checkContains(vasp, "verify_hessian_sign(",
                      "and that convention is verified, not assumed");
        checkContains(vasp, "encut=520", "the configured cutoff reaches VASP");
        checkContains(vasp, "xc='PBEsol'", "as does the functional");
        // The dielectric block VASP prints FIRST is the one without local
        // field effects; picking it would land a tens-of-percent error whole
        // in dalpha/du.
        checkContains(vasp, "\\(including local field effects",
                      "eps_inf is read from the local-field-corrected block");
        check(!contains(vasp, "from ase.vibrations import Vibrations"),
              "no finite-difference Hessian on the VASP path");
        check(!contains(vasp, "BORN_CHARGES"),
              "and no inherited Z* selector — the run computes its own");

        RamanIrConfig vaspIr = vaspCfg;
        vaspIr.computeRaman = false;
        const std::string vaspIrOnly = generateRamanIrScript(vaspIr);
        checkContains(vaspIrOnly, "ibrion=8",
                      "an IR-only VASP run still does the DFPT step");
        checkContains(vaspIrOnly, "COMPUTE_RAMAN = False",
                      "and switches the displaced sweep off");
        checkContains(vasp, "ibrion=-1",
                      "the Raman sweep is a separate static LEPSILON run");
        // Guarded at RUN time, not stripped at generation time. The reviewed
        // script is editable, and a user who flips COMPUTE_RAMAN in it should
        // get a working Raman run rather than a flag that controls nothing.
        checkContains(vaspIrOnly, "if COMPUTE_RAMAN:",
                      "the sweep stays in the script behind a live switch");

        RamanIrConfig qeCfg = base;
        qeCfg.calculator.calculator = CalculatorKind::QuantumEspresso;
        qeCfg.calculator.qeEcutwfcRy = 90.0;
        qeCfg.calculator.qeEcutrhoRy = 360.0;
        qeCfg.calculator.espressoPseudoDir = "/opt/qe/pseudo";
        const std::string qe = generateRamanIrScript(qeCfg);

        checkContains(qe, "epsil = .true.",
                      "ph.x is asked for the dielectric response");
        checkContains(qe, "lraman = .true.",
                      "and, for Raman, the analytic third-order response");
        checkContains(qe, "'ecutwfc': 90", "the dual cutoff reaches pw.x");
        checkContains(qe, "'ecutrho': 360", "both halves of it");
        checkContains(qe, "/opt/qe/pseudo",
                      "and the configured pseudopotential library");
        checkContains(qe, "Rydberg / Bohr ** 2",
                      "the .dyn force constants are converted to eV/A^2");
        checkContains(qe, "'occupations': 'fixed'",
                      "epsil is legal only for an insulator");
        // The one restriction that will actually stop a user, named where they
        // will read it rather than left to ph.x's own diagnostics.
        checkContains(qe, "NORM-CONSERVING",
                      "the lraman pseudopotential restriction is stated");
        check(!contains(qe, "ibrion"), "no VASP tags on the QE path");
        check(!contains(qe, "from ase.vibrations import Vibrations"),
              "and no finite-difference Hessian either");

        RamanIrConfig qeIr = qeCfg;
        qeIr.computeRaman = false;
        const std::string qeIrOnly = generateRamanIrScript(qeIr);
        checkContains(qeIrOnly, "COMPUTE_RAMAN = False",
                      "an IR-only QE run switches the Raman tensor off");
        checkContains(qeIrOnly, "epsil = .true.",
                      "but still asks for Z*");
        // Same live switch as the other two engines: lraman is written into
        // ph.in from a run-time branch, not omitted by the generator, so the
        // flag means one thing across all three scripts.
        checkContains(qeIrOnly,
                      "    if COMPUTE_RAMAN:\n"
                      "        handle.write('  lraman = .true.\\n')",
                      "and the ph.in flag is behind the same live switch");

        // One physics core, three front ends. If any of these drifts out of a
        // branch, that engine has quietly grown its own spectroscopy.
        for (const auto& [script, engine] :
             {std::pair{vasp, "VASP"}, std::pair{qe, "Quantum ESPRESSO"}}) {
            checkContains(script, "def mass_weighted_modes(",
                          std::string(engine) + " shares the diagonalization");
            checkContains(script, "def ir_intensities(",
                          std::string(engine) + " shares the Z* contraction");
            checkContains(script, "def raman_activities(",
                          std::string(engine) + " shares the Placzek invariants");
            checkContains(script, "CALANGO_RESULT raman_ir=raman_ir.json",
                          std::string(engine) + " writes the same result file");
            checkContains(script, "'engine': ENGINE",
                          std::string(engine)
                              + " records which route produced it");
        }
        // DFPT is an analytic derivative — reporting a displacement amplitude
        // for it would describe a step that was never taken.
        checkContains(qe, "REPORTED_DELTA = 0.0",
                      "QE reports no displacement, because it took none");
        checkContains(vasp, "REPORTED_DELTA = DELTA if COMPUTE_RAMAN else 0.0",
                      "VASP reports one only for the half that used it");

        CalculatorConfig unsupported;
        unsupported.calculator = CalculatorKind::Mace;
        RamanIrConfig mace = base;
        mace.calculator = unsupported;
        checkContains(generateRamanIrScript(mace),
                      "raise RuntimeError(",
                      "an engine with no response function refuses up front");
    }

    // -- Tetrahedron integration on a cell with no compliant mesh ------------
    //
    // Reported against 3R-NbS2 (rhombohedral, alpha = 30.65 deg): the run died
    // with "no even Gamma-centred grid from (10, 10, 10) up to (34, 34, 34)
    // satisfies that for this cell". The search was not too small — the IBZ
    // vertices of a rhombohedral zone sit at positions fixed by the cell angle
    // (0.1838, 0.3419, 0.6581 here), and a Monkhorst-Pack grid only reaches
    // rationals n/N, so no grid of ANY size can contain them. GPAW's own
    // optimal_monkhorst_pack_grid(contains_ibz_vertices=True) fails there too.
    //
    // GPAW imposes the requirement only while it uses symmetry to reduce the
    // response integration domain (chi0_base.get_integrator_cls guards the
    // check with `if not self.qsymmetry.disabled`), so qsymmetry=False lifts
    // it and the tetrahedron integrator runs. Symmetry reduction gives way,
    // NOT the integrator the user asked for.
    std::printf("Tetrahedron integration without a compliant mesh:\n");
    {
        OpticsConfig cfg;
        cfg.baselineDensityPath = "/jobs/proc_1/single_point.gpw";
        cfg.tetrahedronIntegration = true;
        const std::string tetra = generateOpticsScript(cfg);

        checkContains(tetra, "_tetra_qsymmetry = True",
                      "symmetry reduction is the default");
        checkContains(tetra, "_tetra_qsymmetry = False",
                      "and is what gives way when no compliant mesh exists");
        checkContains(tetra, "qsymmetry=_qsym,",
                      "the flag reaches DielectricFunction");
        checkContains(tetra, "df = _make_df(_tetra_qsymmetry)",
                      "which is called with it");
        // The regression itself: exhausting the search must not be fatal, and
        // must not silently demote the user to the other integrator.
        checkNotContains(tetra, "turn tetrahedron integration off",
                         "exhausting the search no longer tells the user to "
                         "give up tetrahedron integration");
        checkNotContains(tetra, "_chosen is None:\n        raise RuntimeError",
                         "nor raises on it");
        checkContains(tetra, "results_meta[\"qsymmetry\"]",
                      "and what was actually used is recorded in the results");
        // A disagreement between the predicate and the response module gets
        // the same remedy rather than an abort.
        checkContains(tetra, "df = _make_df(False)",
                      "a late IBZ-vertex rejection is retried, not raised");

        // Point integration carries none of this: it has no vertex
        // requirement, so passing qsymmetry there would be noise.
        OpticsConfig pointCfg = cfg;
        pointCfg.tetrahedronIntegration = false;
        const std::string point = generateOpticsScript(pointCfg);
        checkNotContains(point, "qsymmetry",
                         "point integration never mentions qsymmetry");
        checkContains(point, "df = _make_df()",
                      "and builds the response with the default");

        // -- Intraband (Drude) free-carrier term --------------------------
        // 3R-NbS2 is a metal (two bands cross E_F), and the generator used to
        // hardcode intraband=False labelled "semiconductor", silently
        // dropping the free-carrier response that dominates a metal below the
        // interband onset. GPAW gates the term on `self.gs.metallic and
        // intraband` (chi0.py), so True is right for both cases and no
        // metallicity guess belongs in the generated script: verified as a
        // bitwise no-op on gapped Si and as the Drude term appearing on
        // NbS2 (eps_1(0) 11.5 -> 1.1e4).
        for (const auto& [script, mode] :
             {std::pair{tetra, "tetrahedron"}, std::pair{point, "point"}}) {
            checkContains(script, "_intraband = True",
                          std::string(mode)
                              + " integration includes the free-carrier term");
            checkContains(script, "intraband=_intraband,",
                          std::string(mode)
                              + " passes it to DielectricFunction");
            // rate=0.0 is DielectricFunction's own default and trips
            // `assert chi0_drude.zd.upper_half_plane` on any metal, so a
            // non-zero rate is a correctness requirement, not a preference.
            checkContains(script, "_drude_rate = \"eta\"",
                          std::string(mode)
                              + " ties the Drude rate to the broadening "
                                "rather than leaving it at zero");
            checkContains(script, "rate=_drude_rate,",
                          std::string(mode) + " passes that rate through");
            checkNotContains(script, "_intraband = False",
                             std::string(mode)
                                 + " no longer assumes a semiconductor");
            checkContains(script, "\"intraband\": bool(_intraband)",
                          std::string(mode)
                              + " records the term in the results metadata");
        }

        // -- Relaxation time ------------------------------------------------
        // eta is a plotting choice; a scattering time is a material property.
        // Tying them is the safe default but cannot describe a measured Drude
        // edge, so the rate is settable from tau — with the factor of two
        // that GPAW's convention demands.
        OpticsConfig tau = cfg;
        tau.tetrahedronIntegration = false;
        tau.drudeRateFromBroadening = false;
        tau.drudeRelaxationTimeFs = 9.3;
        const std::string tauScript = generateOpticsScript(tau);
        checkContains(tauScript, "_drude_tau_fs = 9.3",
                      "the relaxation time reaches the script");
        // The conversion is emitted as an expression, not a pre-multiplied
        // number: this factor of two is the whole trap, and a reader has to
        // be able to check it without recomputing it.
        checkContains(tauScript, "_hbar_eV_fs = 0.6582119569",
                      "with hbar in eV*fs spelled out");
        checkContains(tauScript, "_drude_rate = _hbar_eV_fs / (2.0 * _drude_tau_fs)",
                      "and rate = hbar/(2*tau) derived in the script itself");
        checkNotContains(tauScript, "_drude_rate = \"eta\"",
                         "an explicit tau replaces the eta idiom rather than "
                         "sitting beside it");
        checkContains(tauScript, "\"drude_tau_fs\": _drude_tau_fs",
                      "and tau is recorded in the results metadata");

        // -- Drude off --------------------------------------------------------
        // Distinct from "gapped": GPAW already skips the term on a gapped
        // system, so off is a statement about a METAL and is warned about.
        OpticsConfig noDrude = cfg;
        noDrude.tetrahedronIntegration = false;
        noDrude.intrabandDrude = false;
        const std::string offScript = generateOpticsScript(noDrude);
        checkContains(offScript, "_intraband = False",
                      "the term can be switched off");
        checkContains(offScript, "CALANGO_WARN intraband (Drude) term DISABLED",
                      "and says so, since on a metal it changes the physics");
        // Still passed, still valid: nothing downstream should depend on the
        // rate being absent when the term is off.
        checkContains(offScript, "rate=_drude_rate,",
                      "the rate argument stays well-formed with the term off");
    }

    // -- GPAW eigensolver / mode pairing ------------------------------------
    //
    // Two bugs, both of which reached a user as a traceback several hundred
    // lines into a run:
    //
    //   1. The generator emitted eigensolver="direct", which is not a name in
    //      GPAW's registry at all (gpaw/old/eigensolvers/__init__.py maps
    //      'lcao' -> DirectLCAO). KeyError: 'direct', in every mode.
    //   2. It emitted the chosen solver regardless of the MODE. GPAW couples
    //      them with an assertion: in LCAO mode only DirectLCAO/LCAOETDM are
    //      admissible, so Davidson, CG and RMM-DIIS all abort. They iterate
    //      wavefunctions on a grid or plane-wave basis, which an LCAO run
    //      does not have.
    //
    // Verified against gpaw 26.7.1b1: lcao+lcao runs, lcao+dav fails, pw+dav
    // runs.
    std::printf("GPAW eigensolver / mode pairing:\n");
    {
        // "direct" must never appear again, in any mode.
        for (const auto [mode, name] :
             {std::pair{GpawMode::Lcao, "lcao"},
              std::pair{GpawMode::PlaneWave, "pw"},
              std::pair{GpawMode::FiniteDifference, "fd"}}) {
            CalculatorConfig c = gpawConfig();
            c.gpawMode = mode;
            c.gpawEigensolver = GpawEigensolver::Direct;
            const std::string script =
                AseScriptGenerator::generate(c, "structure.extxyz");
            checkNotContains(script, "eigensolver=\"direct\"",
                             std::string(name)
                                 + " mode never emits the non-existent "
                                   "\"direct\" name");
        }

        // LCAO mode: whatever was chosen, the emitted solver is the LCAO one.
        for (const auto solver :
             {GpawEigensolver::Davidson, GpawEigensolver::ConjugateGradient,
              GpawEigensolver::RmmDiis, GpawEigensolver::Direct}) {
            CalculatorConfig c = gpawConfig();
            c.gpawMode = GpawMode::Lcao;
            c.gpawEigensolver = solver;
            checkContains(AseScriptGenerator::generate(c, "structure.extxyz"),
                          "eigensolver=\"lcao\",",
                          "LCAO mode always emits the direct LCAO solver");
        }

        // And the converse: the LCAO solver must not leak into a grid or
        // plane-wave run, where it has no basis to diagonalize over.
        for (const auto [mode, name] :
             {std::pair{GpawMode::PlaneWave, "plane-wave"},
              std::pair{GpawMode::FiniteDifference, "finite-difference"}}) {
            CalculatorConfig c = gpawConfig();
            c.gpawMode = mode;
            c.gpawEigensolver = GpawEigensolver::Direct;
            const std::string script =
                AseScriptGenerator::generate(c, "structure.extxyz");
            checkNotContains(script, "eigensolver=\"lcao\"",
                             std::string(name)
                                 + " mode does not use the LCAO solver");
            checkContains(script, "eigensolver=\"dav\",",
                          std::string(name)
                              + " mode falls back to GPAW's own default");
        }

        // A coerced choice is stated in the script, not applied silently.
        CalculatorConfig coerced = gpawConfig();
        coerced.gpawMode = GpawMode::Lcao;
        coerced.gpawEigensolver = GpawEigensolver::Davidson;
        checkContains(AseScriptGenerator::generate(coerced, "structure.extxyz"),
                      "# Eigensolver set to \"lcao\" for this mode",
                      "and a substituted solver says so in the script");

        // The ordinary pairings are untouched.
        CalculatorConfig plain = gpawConfig();
        plain.gpawMode = GpawMode::PlaneWave;
        plain.gpawEigensolver = GpawEigensolver::RmmDiis;
        const std::string plainScript =
            AseScriptGenerator::generate(plain, "structure.extxyz");
        checkContains(plainScript, "eigensolver=\"rmm-diis\",",
                      "a valid pairing is emitted unchanged");
        checkNotContains(plainScript, "# Eigensolver set to",
                         "and carries no substitution note");
    }

    // -- Cluster expansion: the design matrix -------------------------------
    //
    // The ECI fit regresses energies against cluster correlations, and until
    // now cluster_expansion.json carried the energies and not the
    // correlations — so the fitter had a right-hand side and no matrix. These
    // pin the matrix into the file, and pin the guards that stop a
    // MISALIGNED one being used, which is the failure that matters: a design
    // matrix silently truncated or belonging to another ensemble still fits,
    // and still produces ECIs that look like physics.
    std::printf("Cluster expansion design matrix:\n");
    {
        ClusterExpansionRunConfig ce;
        ce.calculator = gpawConfig();
        ce.correlations = {{1.0, 0.5, -0.25}, {1.0, -0.5, 0.25}};
        ce.orbitLabels = {"empty", "pair r=2.55 m=12", "triplet r=2.55"};
        const std::string script =
            ClusterExpansionScriptGenerator::generate(ce);
        checkContains(script, "correlations = [",
                      "the correlations are emitted as the design matrix");
        checkContains(script, "orbit_labels = [\"empty\"",
                      "with a label per column, so an ECI can be attributed "
                      "to a cluster rather than to a column number");
        checkContains(script, "0.5, -0.25",
                      "at full precision — rounding the regressor biases "
                      "every fitted interaction");
        checkContains(script, "record[\"correlation\"] = correlations[index]",
                      "and each configuration carries its own row, joined by "
                      "frame index");
        checkContains(script, "\"orbit_labels\": orbit_labels",
                      "the labels reach cluster_expansion.json");
        // The guards. Both are fatal rather than best-effort.
        checkContains(script, "correlation rows for",
                      "a row count that disagrees with the trajectory is "
                      "fatal, not zipped to the shorter of the two");
        checkContains(script, "design matrix is ragged",
                      "and so is a ragged matrix");

        ClusterExpansionRunConfig bare = ce;
        bare.correlations.clear();
        bare.orbitLabels.clear();
        const std::string without =
            ClusterExpansionScriptGenerator::generate(bare);
        checkContains(without, "cluster_correlations.json",
                      "without an embedded matrix the script falls back to a "
                      "sidecar beside the trajectory");
        checkContains(without, "except FileNotFoundError",
                      "guarded with try/except rather than os.path.exists — "
                      "this script never imports os, and a bare os. here "
                      "would be a NameError that byte-compiling cannot see");
        checkContains(without, "CALANGO_WARN no cluster correlations",
                      "and says plainly that no ECI fit is possible from the "
                      "result, rather than writing a file that merely lacks "
                      "a key");
    }

    // -- Electron-phonon coupling -------------------------------------------
    //
    // The module's value is not the matrix elements — it is what is derived
    // from them, and specifically that tau comes out in a form the Drude term
    // in the optics module can consume. These assertions pin that chain and
    // the two guards that keep an expensive run from being wasted.
    std::printf("Electron-phonon coupling:\n");
    {
        ElectronPhononConfig cfg;
        cfg.calculator = gpawConfig();
        const std::string script = generateElectronPhononScript(cfg);

        // The three GPAW stages, in order.
        checkContains(script, "from gpaw.elph import DisplacementRunner",
                      "stage 1 uses GPAW's displacement runner");
        checkContains(script, "_sc.calculate_supercell_matrix(_calc2)",
                      "stage 2 projects dV/du onto the LCAO basis");
        checkContains(script, "_epm.bloch_matrix(_calc3, k_qc=_qs",
                      "stage 3 rotates into the Bloch basis");
        // LCAO is a requirement of the method, not a speed choice: the
        // supercell stage projects onto basis functions.
        checkContains(script, "mode=\"lcao\"",
                      "throughout in LCAO, which the projection requires");
        // prefactor=True is what puts g in eV. Without it every derived
        // quantity is wrong by a mode-dependent factor, silently.
        checkContains(script, "prefactor=True",
                      "with the sqrt(hbar/2Mw) prefactor, so g is in eV");
        // Symmetry off in stage 3: bloch_matrix indexes the full k-set.
        checkContains(script, "symmetry=\"off\"",
                      "and no symmetry reduction where the k-set is indexed");

        // The handoff. The script's job now ENDS at the raw arrays: alpha^2F,
        // lambda and tau are computed by Calango (ElectronPhononAnalysis),
        // which integrates both Fermi-surface deltas on tetrahedra.
        //
        // This replaced an in-script Gaussian whose lambda on fcc Al ran
        // 0.009, 0.22, 0.49, 1.55, 4.99, 16.6, 31.0 as sigma_e was widened
        // 16x, with no plateau — the reported number was whatever sigma_e was
        // set to. Pinned as absent so it cannot come back.
        checkNotContains(script, "_gaussian(",
                         "no Gaussian smearing survives in the script — the "
                         "Fermi-surface integration moved to tetrahedra, "
                         "which have no width to converge");
        checkNotContains(script, "lambda vs Fermi smearing",
                         "and with it the smearing sweep that used to police "
                         "a parameter that no longer exists");
        checkContains(script, "np.save(\"elph_eigenvalues.npy\"",
                      "the eigenvalues are saved for the analysis");
        checkContains(script, "np.save(\"elph_kplusq.npy\"",
                      "so is the k+q map, built once here rather than twice");
        // The RAW frequencies: Calango masks and counts imaginary modes
        // itself, and a placeholder written here would hide an unstable
        // structure behind a plausible lambda.
        checkContains(script, "np.save(\"elph_frequencies.npy\"",
                      "and the phonon frequencies as they are, negatives "
                      "included");
        checkNotContains(script, "_omega_ql = np.where(_valid_ql",
                         "never with imaginary modes replaced by a "
                         "placeholder before saving");
        // The 2*pi ASE's reciprocal() omits. Without it every gradient, so
        // every tetrahedron weight, so N(E_F) and lambda, is off by (2pi)^3.
        checkContains(script, "2.0 * np.pi * np.array(atoms.cell.reciprocal())",
                      "the reciprocal vectors carry the 2*pi that ASE's "
                      "reciprocal() leaves out");
        // The biggest array is referenced where GPAW already wrote it rather
        // than recopied: it is tens of gigabytes on a production mesh.
        checkContains(script, "gsquared gsqklnn.npy",
                      "and |g|^2 is read from GPAW's own gsqklnn.npy, not "
                      "rewritten");
        checkContains(script, "savetofile=True",
                      "which stage 3 is therefore asked to write");
        // The mass convention is an OPEN question, not a settled one: GPAW
        // divides by one atomic mass unit and flags its own uncertainty about
        // it. Pinned as a comment so the next reader finds the question
        // rather than rediscovering it from a wrong lambda.
        checkContains(script, "potential BUG",
                      "and the open question about which mass belongs in the "
                      "prefactor travels with the code");

        // The manifest the C++ loader reads.
        checkContains(script, "calango.elph.raw 1",
                      "a manifest ties the arrays to the mesh they live on");
        // mu* travels in the manifest rather than being defaulted at analysis
        // time: T_c depends on it exponentially, so a run analysed later must
        // use the value the run was configured with.
        checkContains(script, "mu_star 0.1",
                      "and carries mu*, which T_c depends on exponentially");
        checkContains(script, "CALANGO_RESULT elph=elph_raw.txt",
                      "and is announced as the run's result");
        // The ordering assumption, checked rather than trusted: GPAW's BZ
        // enumeration is row-major today, and if it ever changed every
        // eigenvalue would attach to the wrong corner of the mesh while still
        // producing a number.
        checkContains(script, "k-point order is not the row-major grid order",
                      "the k-point ordering the tetrahedra assume is verified "
                      "in the script, not assumed");

        // Guards. Both exist to fail in the first seconds rather than after
        // the 6N+1 displacement runs have been paid for.
        checkContains(script, "is denser than the supercell",
                      "an incommensurate q-mesh is refused up front");
        checkContains(script, "is not on the k-mesh",
                      "and so is a k-mesh that does not contain k+q");
        // A gapped system has no Fermi surface for this to be about. That
        // refusal now lives in ElectronPhononAnalysis, where its own test
        // covers it — the script no longer decides anything about E_F.
        // Imaginary modes are a real result, but 1/w cannot represent them.
        checkContains(script, "imaginary phonon",
                      "imaginary modes are reported, not silently integrated");
        // Resume safety. Stage 1 runs for hours and therefore gets
        // interrupted; ASE decides which displacements are done by which
        // files EXIST, so a zero-length file left by a kill counts as done
        // and that displacement is silently skipped on the rerun. ASE's own
        // docstring says the file must be deleted and does not delete it.
        checkContains(script, "strip_empties()",
                      "an interrupted run's empty cache files are cleared "
                      "before resuming");
        checkContains(script, "cleared {_stripped} empty displacement",
                      "and the recomputation is reported rather than silent");

        // The settings must actually reach the script.
        ElectronPhononConfig custom = cfg;
        custom.supercell[0] = custom.supercell[1] = custom.supercell[2] = 3;
        custom.qGrid[0] = custom.qGrid[1] = custom.qGrid[2] = 3;
        custom.kGrid[0] = custom.kGrid[1] = custom.kGrid[2] = 12;
        custom.temperatureK = 77.0;
        custom.basis = "sz(dzp)";
        const std::string tuned = generateElectronPhononScript(custom);
        checkContains(tuned, "SUPERCELL = (3, 3, 3)", "the supercell is used");
        checkContains(tuned, "QGRID = (3, 3, 3)", "so is the q-mesh");
        checkContains(tuned, "KGRID = (12, 12, 12)", "and the k-mesh");
        checkContains(tuned, "temperature 77",
                      "and the temperature tau is asked at reaches the "
                      "manifest");
        checkContains(tuned, "BASIS = \"sz(dzp)\"", "and the LCAO basis");
    }

    // -- 2D optics: the same advanced options as 3D -------------------------
    //
    // "2D Optics" is the SAME wizard and the SAME generator with vacuumAxis
    // set, so tetrahedron integration and the Drude term are not ported into
    // it — they are structurally shared. What is worth pinning is that the
    // sharing actually holds, because a future 2D-only branch that skipped
    // either would be invisible until a monolayer came out wrong.
    std::printf("2D optics inherits the 3D response options:\n");
    {
        OpticsConfig sheet;
        sheet.baselineDensityPath = "/jobs/proc_1/single_point.gpw";
        sheet.vacuumAxis = 2;
        sheet.tetrahedronIntegration = true;
        sheet.drudeRateFromBroadening = false;
        sheet.drudeRelaxationTimeFs = 20.0;
        sheet.responseKpts[0] = sheet.responseKpts[1] = 12;
        const std::string script = generateOpticsScript(sheet);

        checkContains(script, "twod_observables",
                      "the sheet observables are derived");
        checkContains(script, "L_z = float(atoms.cell.lengths()[2])",
                      "from the chosen vacuum axis");
        checkContains(script, "integrationmode = \"tetrahedron integration\"",
                      "tetrahedron integration is available in 2D");
        checkContains(script, "_intraband = True",
                      "so is the free-carrier term");
        checkContains(script, "_drude_tau_fs = 20",
                      "and its relaxation time");
        // The mesh search must leave the vacuum axis alone. A sheet sampled
        // in its vacuum direction is not a denser sheet, and forcing that
        // axis even would double the k-points for nothing — the guard is
        // GPAW's own pbc flags rather than the wizard's vacuumAxis, so it
        // still holds for a baseline whose periodicity disagrees.
        checkContains(script, "if _pbc[_i]",
                      "the evenness constraint applies only to periodic axes");
        checkContains(script, "_axis_vals.append([int(_n)])",
                      "and a non-periodic axis is never grown");
    }

    // -- K-point convergence: the plasma-frequency target --------------------
    //
    // The fourth convergence metric, and the only one that is not free: ΔE,
    // the forces and the band energies are read off the SCF the sweep already
    // ran, while ω_p costs a response evaluation per mesh. So it is opt-in,
    // and the OFF branch must stay byte-for-byte the sweep it always was —
    // otherwise every existing k-point study silently changes cost.
    std::printf("K-point convergence, plasma-frequency target:\n");
    {
        KpointsConvergenceRunConfig off;
        off.calculator = gpawConfig();
        off.meshes = {{{2, 2, 2}, 2}, {{4, 4, 4}, 4}};
        const std::string without =
            KpointsConvergenceScriptGenerator::generate(off);

        KpointsConvergenceRunConfig on = off;
        on.plasmaFrequency = true;
        const std::string with =
            KpointsConvergenceScriptGenerator::generate(on);

        checkNotContains(without, "plasma_frequency",
                         "the sweep says nothing about omega_p unless asked");
        checkContains(with, "def plasma_frequency(calc, tag):",
                      "the helper is emitted when the target is on");
        checkContains(with, "from gpaw.response.chi0_drude import "
                            "Chi0DrudeCalculator",
                      "and reaches GPAW's Drude calculator for it");
        checkContains(with, "record[\"plasma_frequency_eV\"] = _wp",
                      "every mesh records its omega_p");
        checkContains(with, "delta_plasma_frequency_eV",
                      "and its drift from the densest mesh");
        checkContains(with, "summary[\"plasma_frequency\"] = True",
                      "the summary flag the results window keys its fourth "
                      "panel on");
        // A gapped system has no intraband term at all. Recording the reason
        // is the difference between "not applicable" and a converged zero.
        checkContains(with, "if not gs.metallic:",
                      "a gapped ground state is detected rather than "
                      "returning a converged-looking 0.0");
        checkContains(with, "plasma_frequency_note",
                      "and the reason is carried into the results");
        // Tetrahedron first, with the qsymmetry retry, then point
        // integration: the ladder that keeps the better integrator wherever
        // it can run. Point integration alone would need a far denser mesh
        // for the same number.
        checkContains(with, "(\"tetrahedron integration\", True),",
                      "tetrahedron integration is tried first");
        checkContains(with, "(\"tetrahedron integration\", False),",
                      "then without symmetry reduction of the response");
        checkContains(with, "(\"point integration\", True))",
                      "with point integration only as the last resort");
        checkContains(with, "record[\"plasma_integration\"] = _wp_mode",
                      "and whichever ran is recorded, since two meshes "
                      "measured by different integrators are not comparable");

        // VASP goes through a different branch of this generator with no
        // route to GPAW's response module, so the request is dropped rather
        // than emitted as code that cannot run.
        KpointsConvergenceRunConfig vasp = on;
        vasp.calculator.calculator = CalculatorKind::Vasp;
        checkNotContains(KpointsConvergenceScriptGenerator::generate(vasp),
                         "plasma_frequency",
                         "the VASP sweep drops the target instead of emitting "
                         "GPAW response code it cannot run");
    }

    // -- Nonlinear optics (gpaw.nlopt) ---------------------------------------
    //
    // The API surface is the part that cannot be checked by reading: these are
    // real function names and keyword arguments in someone else's package, and
    // a misspelling surfaces as a TypeError after the ground state has been
    // paid for.
    std::printf("Nonlinear optics:\n");
    {
        NonlinearOpticsConfig cfg;
        cfg.calculator.calculator = CalculatorKind::Gpaw;
        cfg.calculator.planeWaveCutoffEv = 800.0;
        cfg.components = {"yyy", "xxy"};
        const std::string script = generateNonlinearOpticsScript(cfg);

        checkContains(script, "from gpaw.nlopt.matrixel import make_nlodata",
                      "the matrix elements come from gpaw.nlopt");
        checkContains(script, "from gpaw.nlopt.shg import get_shg",
                      "and SHG from its own module");
        checkContains(script, "nlodata = make_nlodata('gs.gpw'",
                      "make_nlodata is handed the .gpw path, as the tutorial "
                      "does");
        checkContains(script, "nlodata.write('mml.npz')",
                      "the matrix elements are saved for reuse");
        checkContains(script,
                      "get_shg(nlodata, freqs=freqs, eta=ETA, pol=_pol",
                      "get_shg is called with the documented signature");
        checkContains(script, "gauge=GAUGE", "including the gauge");
        checkContains(script, "COMPONENTS = ['yyy', 'xxy']",
                      "every requested component is evaluated");
        checkContains(script, "mode=PW(800)",
                      "the ground-state cutoff is the one that was configured");

        // The three ground-state settings the METHOD requires. Each is
        // imposed by the generator rather than left to the calculator page,
        // and the first of them fails as a bare AssertionError if it is not.
        checkContains(script,
                      "symmetry={\"point_group\": False, \"time_reversal\": "
                      "True}",
                      "point-group symmetry is off, time reversal kept");
        check(!contains(script, "symmetry=\"off\""),
              "not the stronger symmetry=off, which doubles the k-points");
        checkContains(script, "nbands=\"nao\"",
                      "the band set is large enough to sum over");
        checkContains(script, "\"bands\": -10",
                      "and those empty bands are actually converged");
        checkContains(script, "parallel={'domain': 1}",
                      "make_nlodata needs an undistributed domain");
        checkContains(script, "except AssertionError as exc:",
                      "make_nlodata's bare assert is turned into a message");

        // chi^(2) vanishes identically in a centrosymmetric crystal, and what
        // a finite k-mesh returns there looks exactly like a spectrum.
        checkContains(script, "def has_inversion_symmetry(",
                      "the cell is tested for an inversion centre");
        checkContains(script, "CALANGO_WARN this cell has an inversion centre",
                      "and the user is told before anything is computed");

        // Units. GPAW returns SI base units; the literature quotes neither.
        checkContains(script, "chi.real * 1e12",
                      "m/V -> pm/V for the bulk susceptibility");
        checkContains(script, "sheet.real * 1e18",
                      "and chi*L -> nm^2/V for a sheet");
        checkContains(script, "CALANGO_RESULT nlopt=nlopt.json",
                      "the result marker the controller watches for");

        // Off by default: each is a separate sum over bands and k-points.
        check(!contains(script, "from gpaw.nlopt.shift import get_shift"),
              "the shift current is not computed unless asked for");
        check(!contains(script, "from gpaw.nlopt.linear import get_chi_tensor"),
              "nor the linear tensor");

        NonlinearOpticsConfig all = cfg;
        all.computeShift = true;
        all.computeLinear = true;
        all.gauge = NlOpticsGauge::Velocity;
        all.scissorsEv = 0.7;
        all.bandsFirst = 4;
        all.bandsLast = -8;
        all.vacuumAxis = 2;
        const std::string full = generateNonlinearOpticsScript(all);
        checkContains(full, "from gpaw.nlopt.shift import get_shift",
                      "the shift current is added on request");
        checkContains(full, "from gpaw.nlopt.linear import get_chi_tensor",
                      "as is the linear tensor");
        checkContains(full, "GAUGE = 'vg'", "the velocity gauge is selectable");
        checkContains(full, "ESHIFT = 0.7",
                      "the scissors shift reaches every response");
        checkContains(full, "'eshift_eV': ESHIFT,",
                      "and is recorded, so a spectrum never hides one");
        checkContains(full, "_band_kwargs['ni'] = BAND_FIRST",
                      "the band window is passed to make_nlodata");
        checkContains(full, "VACUUM_AXIS = 2",
                      "and the sheet conversion knows which axis is vacuum");

        // A typo must not cost a ground state: get_shg turns each component
        // into an index with 'xyz'.index() and raises on anything else.
        NonlinearOpticsConfig typo = cfg;
        typo.components = {"YYY", "yy", "abc", "yyy", "xyzz"};
        const std::string filtered = generateNonlinearOpticsScript(typo);
        checkContains(filtered, "COMPONENTS = ['yyy']",
                      "invalid components are filtered and case normalized");
        NonlinearOpticsConfig empty = cfg;
        empty.components.clear();
        checkContains(generateNonlinearOpticsScript(empty),
                      "COMPONENTS = ['yyy']",
                      "an empty list falls back rather than emitting []");

        NonlinearOpticsConfig wrongEngine = cfg;
        wrongEngine.calculator.calculator = CalculatorKind::Vasp;
        const std::string refused = generateNonlinearOpticsScript(wrongEngine);
        checkContains(refused, "raise RuntimeError(",
                      "a non-GPAW engine refuses up front");
        check(!contains(refused, "from gpaw.nlopt"),
              "and imports nothing from gpaw.nlopt it could not satisfy");
    }

    // -- 2D bands: high-symmetry points ---------------------------------------
    std::printf("2D bands high-symmetry points:\n");
    {
        TwoDBandsConfig cfg;
        cfg.baselineDensityPath = "/jobs/proc_1/single_point.gpw";
        const std::string script = generateTwoDBandsScript(cfg);
        checkContains(script, "atoms.cell.bandpath(npoints=0)",
                      "labels come from ASE's Bravais-lattice recognition");
        checkContains(script, "if abs(float(_f[2])) > 1e-6:",
                      "points outside the sampled kz = 0 plane are dropped");
        checkContains(script, "'special_points': _special",
                      "and the surviving ones are exported");
        checkContains(script, "_c = _f @ _recip",
                      "in the same Cartesian frame as the surface itself");
    }

    // -- Charged defects (FNV) ------------------------------------------------
    std::printf("Charged defects:\n");
    {
        DefectConfig cfg;
        cfg.calculator.calculator = CalculatorKind::Gpaw;
        cfg.pristinePath = "/jobs/host/single_point.gpw";
        cfg.neutralDefectPath = "/jobs/defect/single_point.gpw";
        cfg.charges = {-1, 1};   // note: no 0
        cfg.species = {{"S", -1, -4.13}};
        cfg.dielectricConstant = 9.5;
        const std::string script = generateDefectScript(cfg);

        // q = 0 is the reference the whole diagram is anchored on, so it is
        // present whatever the caller listed.
        checkContains(script, "CHARGES = [-1, 0, 1]",
                      "q = 0 is inserted and the list sorted");
        checkContains(script, "'symbol': 'S', 'count': -1, 'mu_eV': -4.13",
                      "the exchanged species reach the script");
        checkContains(script, "EPSILON = 9.5", "and the dielectric constant");
        checkContains(script, "q * E_VBM", "E_F is referenced to the host VBM");
        checkContains(script, "charged_defect_corrections",
                      "the FNV correction uses GPAW's own implementation");
        checkContains(script, "if E_GAP <= 1e-3:",
                      "a metallic host is refused — there is no gap to place "
                      "the levels in");
        checkContains(script, "CALANGO_RESULT charged_defects=",
                      "emits the marker the controller watches for");

        // Without the correction the run must still work — that is what makes
        // a supercell-convergence study possible — and must say so.
        DefectConfig raw = cfg;
        raw.applyFnvCorrection = false;
        const std::string uncorrected = generateDefectScript(raw);
        checkContains(uncorrected, "APPLY_FNV = False", "FNV can be disabled");
        checkContains(uncorrected, "CALANGO_WARN FNV correction disabled",
                      "and the omission is reported");
    }

    // -- Charged defects in 2D materials --------------------------------------
    std::printf("Charged defects (2D):\n");
    {
        Defect2dConfig cfg;
        cfg.calculator.calculator = CalculatorKind::Gpaw;
        cfg.pristinePath = "/jobs/host/single_point.gpw";
        cfg.neutralDefectPath = "/jobs/defect/single_point.gpw";
        cfg.charges = {-1, 1};   // note: no 0
        cfg.species = {{"S", -1, -4.13}};
        cfg.epsilonInPlane = 6.9;
        cfg.epsilonOutOfPlane = 2.8;
        cfg.layerThickness = 6.15;
        const std::string script = generateDefect2dScript(cfg);

        checkContains(script, "CHARGES = [-1, 0, 1]",
                      "q = 0 is inserted and the list sorted");
        checkContains(script, "'symbol': 'S', 'count': -1, 'mu_eV': -4.13",
                      "the exchanged species reach the script");
        // The two constants must arrive DISTINCT. A generator that collapsed
        // the profile to a scalar would still produce a plausible number, and
        // the anisotropy is the whole difference between a sheet and a thin
        // piece of bulk.
        checkContains(script, "EPS_PAR = 6.9", "eps_parallel reaches the script");
        checkContains(script, "EPS_PERP = 2.8", "and eps_perp, separately");
        checkContains(script, "THICKNESS = 6.15", "and the layer thickness");
        checkContains(script, "two_d_image_correction",
                      "the 2D correction is the one applied");
        checkNotContains(script, "charged_defect_corrections",
                         "GPAW's BULK FNV helper is NOT used — it assumes a "
                         "scalar epsilon a sheet does not have");
        checkContains(script, "q * E_VBM", "E_F is referenced to the host VBM");
        checkContains(script, "'scheme': '2D image charge",
                      "the output names the scheme rather than leaving the "
                      "viewer to assume FNV");
        checkContains(script, "CALANGO_RESULT charged_defects_2d=",
                      "emits the marker the controller watches for");

        Defect2dConfig raw = cfg;
        raw.applyCorrection = false;
        const std::string uncorrected = generateDefect2dScript(raw);
        checkContains(uncorrected, "APPLY_CORRECTION = False",
                      "the correction can be disabled");
        checkContains(uncorrected, "the formation energies below are "
                                   "UNCORRECTED",
                      "and the omission is reported");
    }

    // -- Wannier Fermi surface -------------------------------------------------
    std::printf("Wannier Fermi surface:\n");
    {
        FermiSurfaceConfig cfg;
        cfg.mlwfDir = "/jobs/proc_9";
        // Three DIFFERENT counts: a single number would pass even if the
        // generator collapsed the mesh back to one axis.
        cfg.gridSamples[0] = 36;
        cfg.gridSamples[1] = 24;
        cfg.gridSamples[2] = 12;
        const std::string script = generateFermiSurfaceScript(cfg);
        checkContains(script, "_nx = 36", "honors the requested k1 count");
        checkContains(script, "_ny = 24", "and k2 independently");
        checkContains(script, "_nz = 12", "and k3 independently");
        checkContains(script, "'samples': [int(_nx), int(_ny), int(_nz)]",
                      "records all three for the viewer");
        checkContains(script, "get_hamiltonian_kpoint",
                      "interpolates H(R) -> H(k) rather than re-running SCF");
        checkContains(script, "(np.arange(_nx) / _nx) - 0.5",
                      "Gamma-centred grid with the upper endpoint excluded");
        checkContains(script, "_meta.get('gpw')",
                      "resolves the wavefunctions the MLWF run recorded");
        checkContains(script, "len(calc.get_ibz_k_points()) < "
                              "len(calc.get_bz_k_points())",
                      "refuses a symmetry-reduced .gpw");
        checkContains(script, "'crosses_fermi'",
                      "records which bands can contribute a sheet");
        checkContains(script, "CALANGO_RESULT fermi_surface=",
                      "emits its result marker");

        FermiSurfaceConfig tiny = cfg;
        tiny.gridSamples[0] = 1;
        tiny.gridSamples[1] = 1;
        tiny.gridSamples[2] = 1;
        const std::string clamped = generateFermiSurfaceScript(tiny);
        checkContains(clamped, "_nx = 4",
                      "a grid too small to triangulate is clamped");
        checkContains(clamped, "_nz = 4", "on every axis");
    }

    // -- Topological invariants ------------------------------------------------
    std::printf("Topological invariants:\n");
    {
        TopologyConfig cfg;
        cfg.mlwfDir = "/jobs/proc_9";
        cfg.direction = 1;
        const std::string script = generateTopologyScript(cfg);
        checkContains(script, "parallel_transport",
                      "the flow comes from GPAW's parallel transport");
        checkContains(script, "_direction = 1", "along the requested axis");
        checkContains(script, "step -= np.round(step)",
                      "the Chern winding is accumulated on the nearest branch");
        checkContains(script, "np.argmax(gaps)",
                      "Z2 follows the largest gap between centres");
        checkContains(script, "'residual'",
                      "reports how far the winding sits from an integer");
        checkContains(script, "CALANGO_WARN the band structure has no gap",
                      "an ungapped manifold is flagged — the integers would "
                      "describe a partition that is not separated");
        checkContains(script, "Z2 assumes TIME-REVERSAL SYMMETRY",
                      "and the symmetry requirement is stated");

        TopologyConfig chernOnly = cfg;
        chernOnly.invariant = TopologicalInvariant::Chern;
        const std::string only = generateTopologyScript(chernOnly);
        checkContains(only, "_want_z2 = False", "Z2 can be skipped");
        checkContains(only, "_want_chern = True", "leaving Chern alone");
    }

    // -- LAMMPS -------------------------------------------------------------
    std::printf("LAMMPS calculator:\n");
    {
        CalculatorConfig lammps;
        lammps.calculator = CalculatorKind::Lammps;
        lammps.lammpsPairStyle = "eam/alloy";
        lammps.lammpsPairCoeff = {"* * Cu_u3.eam.alloy Cu"};
        lammps.lammpsPotentialFiles = {"/potentials/Cu_u3.eam.alloy"};
        lammps.lammpsExtraCommands = {"neighbor 2.0 bin"};

        const std::string library =
            AseScriptGenerator::generate(lammps, "structure.extxyz");
        checkContains(library, "from ase.calculators.lammpslib import LAMMPSlib",
                      "the library interface uses LAMMPSlib");
        checkContains(library, "\"pair_style eam/alloy\"",
                      "pair style reaches the command list");
        checkContains(library, "\"pair_coeff * * Cu_u3.eam.alloy Cu\"",
                      "pair coefficients reach the command list");
        checkContains(library, "\"neighbor 2.0 bin\"",
                      "extra commands reach the command list");
        // The type -> element map is the one thing that fails SILENTLY when it
        // is wrong: LAMMPS addresses species by integer type, so a mismatched
        // order computes a different compound rather than erroring. It must be
        // derived from the structure, never baked in by the wizard.
        checkContains(library, "species = sorted(set(atoms.get_chemical_symbols()))",
                      "the species order is derived from the structure");
        checkContains(library, "atom_types={symbol: index + 1",
                      "and drives the LAMMPS type mapping");

        CalculatorConfig run = lammps;
        run.lammpsInterface = LammpsInterface::Run;
        run.lammpsCommand = "/usr/bin/lmp_serial";
        const std::string executable =
            AseScriptGenerator::generate(run, "structure.extxyz");
        checkContains(executable, "from ase.calculators.lammpsrun import LAMMPS",
                      "the executable interface uses lammpsrun");
        checkContains(executable, "\"units\": \"metal\"",
                      "units are pinned to metal");
        checkContains(executable, "specorder=species",
                      "lammpsrun is given the derived species order");
        checkContains(executable, "ASE_LAMMPSRUN_COMMAND",
                      "the configured binary is exported for ASE");
        // `files` is what makes lammpsrun copy the potential into the scratch
        // directory the pair_coeff path is resolved against; without it the run
        // fails inside LAMMPS with a file-not-found on a path that exists.
        checkContains(executable, "\"files\":",
                      "potential files are staged into lammpsrun's scratch dir");
        check(!contains(executable, "LAMMPSlib"),
              "the executable interface does not also pull in LAMMPSlib");

        // ASE is hard-wired to eV/Angstrom. Any other units style returns
        // numbers in a different scale with nothing to flag it, so the
        // generated script must refuse rather than convert.
        CalculatorConfig realUnits = lammps;
        realUnits.lammpsUnits = "real";
        const std::string refused =
            AseScriptGenerator::generate(realUnits, "structure.extxyz");
        checkContains(refused, "raise RuntimeError(",
                      "a non-metal units style is refused outright");
        check(!contains(refused, "LAMMPSlib(") && !contains(refused, "LAMMPS("),
              "and no calculator is constructed after the refusal");
    }

    // -- xTB ------------------------------------------------------------------
    std::printf("xTB calculator:\n");
    {
        CalculatorConfig xtb;
        xtb.calculator = CalculatorKind::Xtb;
        const std::string script = AseScriptGenerator::calculatorSnippet(xtb);
        checkContains(script, "from xtb.ase.calculator import XTB",
                      "in-process through xtb-python's ASE calculator");
        // The kwarg names ARE xtb-python's documented API — pinned so a
        // mapping table can never drift in between.
        checkContains(script, "method=\"GFN2-xTB\"", "GFN2-xTB is the default");
        checkContains(script, "accuracy=1", "accuracy reaches the constructor");
        checkContains(script, "electronic_temperature=300",
                      "electronic temperature reaches the constructor");
        checkContains(script, "max_iterations=250",
                      "and so does the SCC iteration cap");

        // GFN-FF is a force field: no electrons, so the two SCC knobs must
        // be withheld rather than emitted as settings that change nothing.
        CalculatorConfig ff = xtb;
        ff.xtbMethod = "GFN-FF";
        const std::string forceField = AseScriptGenerator::calculatorSnippet(ff);
        checkContains(forceField, "method=\"GFN-FF\"", "GFN-FF is honored");
        check(!contains(forceField, "electronic_temperature="),
              "no electronic temperature for a method with no electrons");
        check(!contains(forceField, "max_iterations="),
              "and no SCC iteration cap either");
    }

    // -- DFTB+ ----------------------------------------------------------------
    std::printf("DFTB+ calculator:\n");
    {
        CalculatorConfig dftb;
        dftb.calculator = CalculatorKind::DftbPlus;
        dftb.dftbSlakoDir = "/opt/slako/mio-1-1"; // no trailing slash on purpose
        dftb.kpts[0] = 6;
        dftb.kpts[1] = 6;
        dftb.kpts[2] = 4;
        const std::string script = AseScriptGenerator::calculatorSnippet(dftb);
        checkContains(script, "from ase.calculators.dftb import Dftb",
                      "DFTB+ goes through ASE's file-IO calculator");
        // ASE joins '<El>-<El>.skf' onto DFTB_PREFIX verbatim, so the
        // generator must supply the trailing slash the user left off.
        checkContains(script,
                      "os.environ.setdefault(\"DFTB_PREFIX\", "
                      "r\"/opt/slako/mio-1-1/\")",
                      "the Slater-Koster dir is exported with a trailing slash");
        checkContains(script, "kpts=(6, 6, 4)",
                      "the shared k-grid reaches the calculator");
        checkContains(script, "Hamiltonian_SCC=\"Yes\"", "SCC is on by default");
        checkContains(script, "Hamiltonian_SCCTolerance=1e-05",
                      "the charge tolerance is emitted");
        checkContains(script, "Hamiltonian_MaxSCCIterations=100",
                      "and the iteration cap");
        check(!contains(script, "Hamiltonian_Filling"),
              "no Fermi filling block at the 0 K default");

        // DFTB+ reads Temperature in Hartree when no HSD modifier is given,
        // and ASE's keyword scheme cannot write the modifier — the K -> Ha
        // conversion must happen in the open, in the generated script.
        CalculatorConfig fermi = dftb;
        fermi.dftbFillingTemperatureK = 300.0;
        const std::string smeared = AseScriptGenerator::calculatorSnippet(fermi);
        checkContains(smeared, "Hamiltonian_Filling_=\"Fermi\"",
                      "a positive temperature opens the Fermi filling block");
        checkContains(smeared, "Hamiltonian_Filling_Temperature=300 * kB / Hartree",
                      "with the K -> Hartree conversion visible and auditable");
        checkContains(smeared, "from ase.units import Hartree, kB",
                      "and the units it needs imported");

        // Tolerance and iteration cap describe a cycle that does not run, so
        // they must be withheld with SCC rather than written as inert keys.
        CalculatorConfig nonScc = dftb;
        nonScc.dftbScc = false;
        const std::string oneShot = AseScriptGenerator::calculatorSnippet(nonScc);
        checkContains(oneShot, "Hamiltonian_SCC=\"No\"", "non-SCC is honored");
        check(!contains(oneShot, "SCCTolerance"),
              "no tolerance for a cycle that does not run");

        CalculatorConfig blank = dftb;
        blank.dftbSlakoDir.clear();
        const std::string unset = AseScriptGenerator::calculatorSnippet(blank);
        checkContains(unset, "EDIT ME",
                      "a missing Slater-Koster dir is flagged, not defaulted");
    }

    // -- GROMACS --------------------------------------------------------------
    std::printf("GROMACS calculator:\n");
    {
        CalculatorConfig gromacs;
        gromacs.calculator = CalculatorKind::Gromacs;
        gromacs.task = TaskKind::SinglePoint;
        gromacs.gromacsExtraMdp = "rvdw = 1.0\n# a comment\ncoulombtype = PME";
        const std::string script =
            AseScriptGenerator::calculatorSnippet(gromacs);
        checkContains(script, "from ase.calculators.gromacs import Gromacs",
                      "GROMACS goes through ASE's gmx-driving calculator");
        checkContains(script, "force_field=\"oplsaa\"",
                      "the force field reaches pdb2gmx");
        checkContains(script, "water_model=\"spc\"", "and the water model");
        checkContains(script, "command=r\"gmx\"",
                      "the configured gmx binary reaches the calculator");
        // The calculator's own default is a 10000-step cg minimization, which
        // would silently relax before reporting a \"single point\".
        checkContains(script, "\"integrator\": \"md\"",
                      "the .mdp integrator is overridden");
        checkContains(script, "\"nsteps\": \"0\"",
                      "to make mdrun a true single point");
        // The free-form extras are parsed into the dict, comments dropped.
        checkContains(script, "\"rvdw\": \"1.0\"",
                      "extra .mdp lines reach the parameter dict");
        checkContains(script, "\"coulombtype\": \"PME\"",
                      "including ones after a dropped comment line");
        // clean=True sweeps gromacs.??? on construction — a pdb exported
        // first would be deleted, so the order is load-bearing.
        check(script.find("calc = Gromacs(")
                  < script.find("write(\"gromacs.pdb\", atoms)"),
              "the pdb is written AFTER the constructor's clean sweep");
        // The input pipeline is explicit; the calculator does not run it.
        checkContains(script, "calc.generate_topology_and_g96file()",
                      "pdb2gmx builds the topology");
        checkContains(script, "calc.generate_gromacs_run_file()",
                      "and grompp assembles the .tpr");

        // Gromacs.calculate() reruns mdrun on the files already on disk and
        // never rewrites them with the positions ASE moved — an optimizer
        // would evaluate the starting geometry forever. Refuse, not degrade.
        CalculatorConfig moving = gromacs;
        moving.task = TaskKind::GeometryOptimization;
        const std::string refused =
            AseScriptGenerator::calculatorSnippet(moving);
        checkContains(refused, "raise RuntimeError(",
                      "anything but a single point is refused outright");
        check(!contains(refused, "Gromacs("),
              "and no calculator is constructed after the refusal");
    }

    // -- Born effective charges ---------------------------------------------
    std::printf("Born effective charges:\n");
    {
        BornChargesConfig born;
        born.calculator = gpawConfig();
        born.displacement = 0.015;
        const std::string script = generateBornChargesScript(born);
        // GPAW renamed this (get_polarization_phase -> polarization_phase) and
        // changed its signature and return type; binding both spellings is
        // what stops the run dying at import on one GPAW generation.
        checkContains(script, "from gpaw.berryphase import polarization_phase",
                      "binds the modern Berry-phase entry point");
        checkContains(script, "import get_polarization_phase",
                      "and falls back to the legacy spelling");
        checkContains(script, "result['phase_c']",
                      "unwraps the dict the modern call returns");
        checkContains(script, "delta = 0.015", "displacement is honored");
        // The Berry phase is only defined on the unsymmetrized BZ; letting
        // GPAW fold the k-points would silently corrupt the polarization.
        // Spelled by the shared GPAW keyword block, hence the double quotes.
        checkContains(script, "symmetry=\"off\"",
                      "the Berry-phase run disables k-point symmetry folding");

        // The branch fix. The Berry phase is defined modulo 2*pi, and the +u
        // and -u runs routinely land on different branches: without wrapping
        // the DIFFERENCE into (-pi, pi], cubic BN came out at -540 e for boron
        // instead of +1.93, exactly 6 polarization quanta off.
        checkContains(script, "% (2.0 * np.pi) - np.pi",
                      "the phase difference is wrapped onto one branch");
        check(script.find("phase_difference(phases[0], phases[1])")
                  < script.find("np.array(cell)"),
              "and wrapped BEFORE the conversion to Cartesian");
        checkContains(script, "/ (2.0 * delta)",
                      "Z* is a central difference over 2*delta");
        checkContains(script, "born -= residual / len(atoms)",
                      "the acoustic sum rule is imposed by default");
        // ...but only over the WHOLE cell: a partial sum is not a residual.
        checkContains(script, "complete = len(indices) == len(atoms)",
                      "and only when every atom was computed");
        checkContains(script, "'raw_tensor'",
                      "the uncorrected tensors are reported alongside");
        checkContains(script, "CALANGO_RESULT born_charges=born_charges.json",
                      "emits the marker the controller watches for");
        // A non-periodic cell has no macroscopic polarization at all.
        checkContains(script, "if not atoms.pbc.all():",
                      "refuses a non-periodic structure");
    }
    {
        // Baseline workflow: the run starts from a completed Single-Point,
        // taking its geometry and rebuilding every displaced calculator from
        // it — but NOT reusing its density, since Z* is the density's response.
        BornChargesConfig baseline;
        baseline.calculator = gpawConfig();
        baseline.baselinePath = "/jobs/proc_1/single_point.gpw";
        const std::string script = generateBornChargesScript(baseline);
        checkContains(script, "_baseline = GPAW(r\"/jobs/proc_1/single_point.gpw\"",
                      "loads the baseline ground state");
        checkContains(script, "atoms = _baseline.get_atoms()",
                      "and displaces about ITS converged geometry");
        checkContains(script, "_baseline.new(symmetry='off'",
                      "each displaced run is rebuilt from the baseline");
        check(!contains(script, "read('structure.extxyz')"),
              "so the staged structure is not read at all");
        // The one thing a baseline cannot buy here.
        check(!contains(script, "fixed_density"),
              "and there is no fixed-density shortcut");
    }
    {
        BornChargesConfig subset;
        subset.calculator = gpawConfig();
        subset.atomIndices = {0, 2, 5};
        subset.acousticSumRule = false;
        const std::string script = generateBornChargesScript(subset);
        checkContains(script, "indices = [0, 2, 5]", "atom subset is honored");
        check(!contains(script, "born -= residual / len(atoms)"),
              "the sum rule is not imposed when it was turned off");
    }
    {
        // Every other engine must refuse up front rather than burn 6N SCF
        // cycles and fail at the polarization step.
        BornChargesConfig vasp;
        vasp.calculator.calculator = CalculatorKind::Vasp;
        const std::string script = generateBornChargesScript(vasp);
        checkContains(script, "raise RuntimeError",
                      "a non-GPAW engine fails immediately");
        check(!contains(script, "get_polarization_phase"),
              "and never reaches the Berry-phase code");
    }

    // -- Spin-orbit coupling (Electronic Structure) --------------------------
    std::printf("Spin-orbit coupling:\n");
    {
        ElectronicConfig bands;
        bands.backend = ElectronicBackend::Gpaw;
        bands.baselineDensityPath = "/jobs/proc_1/single_point.gpw";
        bands.spinOrbit = true;
        const std::string script = generateElectronicScript(bands);
        checkContains(script, "from gpaw.spinorbit import soc_eigenstates",
                      "SOC comes from GPAW's spinorbit module");
        checkContains(script, "_soc = soc_eigenstates(band_calc)",
                      "applied to the states along the k-path");
        // SOC shifts the Fermi level with the bands; keeping the scalar-
        // relativistic one would misplace the gap in every plot downstream.
        checkContains(script, "efermi = float(_soc.fermi_level)",
                      "the Fermi level is re-read from the spinor solution");
        checkContains(script, "_soc_energies[_np.newaxis]",
                      "spinor bands are ONE channel, not a spin pair");
        // The rebuilt BandStructure must come after the one it replaces.
        check(script.find("bs = band_calc.band_structure()")
                  < script.find("bs = BandStructure("),
              "the SOC bands replace the scalar-relativistic ones");
    }
    {
        ElectronicConfig bands;
        bands.backend = ElectronicBackend::Gpaw;
        bands.baselineDensityPath = "/jobs/proc_1/single_point.gpw";
        const std::string script = generateElectronicScript(bands);
        check(!contains(script, "spinorbit"),
              "SOC costs an extra diagonalization and is emitted only on request");
    }

    // -- MLIP calculator blocks ---------------------------------------------
    std::printf("MLIP calculator blocks:\n");
    {
        CalculatorConfig c;
        c.calculator = CalculatorKind::DeepMd;
        c.deepmdModelPath = "/models/frozen.pb";
        const std::string script = AseScriptGenerator::calculatorSnippet(c);
        checkContains(script, "from deepmd.calculator import DP", "DeepMD import");
        checkContains(script, "model=r\"/models/frozen.pb\"", "frozen graph path");
    }
    {
        CalculatorConfig c;
        c.calculator = CalculatorKind::Allegro;
        c.nequipModelPath = "/models/deployed.pth";
        c.mlipDevice = MlipDevice::Cuda;
        const std::string script = AseScriptGenerator::calculatorSnippet(c);
        // Both nequip generations are bound: >= 0.7 moved the calculator to
        // nequip.integrations.ase and loads nequip-compile artifacts, <= 0.6
        // deployed TorchScript. The script must run on either instead of
        // dying at load on one of them.
        checkContains(script, "from nequip.integrations.ase import NequIPCalculator",
                      "the modern import is tried first");
        checkContains(script, "from nequip.ase import NequIPCalculator",
                      "with the legacy import as the fallback");
        checkContains(script, "NequIPCalculator.from_compiled_model",
                      "nequip >= 0.7 loads the compiled artifact");
        checkContains(script, "NequIPCalculator.from_deployed_model",
                      "nequip <= 0.6 loads the deployed TorchScript");
        checkContains(script, "device=\"cuda\"", "device is honored");
        checkContains(script, "nequip-allegro", "names the Allegro package");
    }
    {
        // A model trained in non-ASE units must not silently claim a factor of
        // 1.0 — that is the failure mode that yields plausible-but-wrong
        // energies with no error anywhere.
        CalculatorConfig c;
        c.calculator = CalculatorKind::NequIp;
        c.nequipEnergyUnits = "kcal/mol";
        const std::string script = AseScriptGenerator::calculatorSnippet(c);
        check(!contains(script, "energy_units_to_eV=1.0"),
              "non-eV training units do not claim a unit conversion of 1.0");
        checkContains(script, "EDIT ME", "and the script says so");
    }
    {
        // CHGNet.load() knows only version strings — model_name="latest"
        // raises ValueError, so "track the installed release" must be spelled
        // by omitting the argument entirely.
        CalculatorConfig c;
        c.calculator = CalculatorKind::ChgNet;
        c.chgnetWeights = ChgNetWeights::Latest;
        const std::string script = AseScriptGenerator::calculatorSnippet(c);
        checkContains(script, "model = CHGNet.load()",
                      "\"latest\" loads the package's own default checkpoint");
        check(!contains(script, "model_name="),
              "and never as a model_name CHGNet.load would reject");
        // stress_weight is the GPa -> eV/Å³ conversion factor, not an on/off
        // switch: 1.0 reported stresses ~160x too large, 0.0 zeroed them
        // silently. The kwarg must be left at CHGNet's own default.
        check(!contains(script, "stress_weight"),
              "the stress conversion factor is never overridden");

        CalculatorConfig pinned = c;
        pinned.chgnetWeights = ChgNetWeights::V0_3_0;
        const std::string reproducible =
            AseScriptGenerator::calculatorSnippet(pinned);
        checkContains(reproducible, "model_name=\"0.3.0\"",
                      "a pinned weight set is named explicitly");
    }
    {
        CalculatorConfig c;
        c.calculator = CalculatorKind::MatterSim;
        c.matterSimModel = MatterSimModel::M100;
        c.matterSimThermal = true;
        c.matterSimTemperatureK = 900.0;
        const std::string script = AseScriptGenerator::calculatorSnippet(c);
        checkContains(script, "MatterSim-v1.0.0-5M.pth", "model size is honored");
        checkContains(script, "temperature_K", "thermodynamic state is emitted");
    }
    {
        CalculatorConfig c;
        c.calculator = CalculatorKind::FairChem;
        c.fairChemModel = FairChemModel::EScn;
        c.fairChemCheckpointPath = "/models/escn.pt";
        c.mlipDevice = MlipDevice::Cuda;
        const std::string script = AseScriptGenerator::calculatorSnippet(c);
        checkContains(script, "OCPCalculator", "FAIRChem calculator");
        checkContains(script, "checkpoint_path=r\"/models/escn.pt\"", "checkpoint");
        checkContains(script, "eSCN", "architecture named in the comment");
        checkContains(script, "cpu=False", "GPU flag follows the device");
    }

    // -- Phonon displacement drivers ----------------------------------------
    std::printf("Phonon displacement drivers:\n");
    {
        PhononConfig p; // defaults: full 6N, no residual removal
        const std::string script =
            PhononScriptGenerator::generate(p, "structure.extxyz");
        checkContains(script, "run_ase_displacements()", "ASE driver runs");
        check(!contains(script, "import phonopy"),
              "phonopy is not imported when symmetry reduction is off");
        check(!contains(script, "ResidualFreeCalculator"),
              "no residual machinery when the option is off");
    }
    {
        PhononConfig p;
        p.symmetryReducedDisplacements = true;
        const std::string script =
            PhononScriptGenerator::generate(p, "structure.extxyz");
        checkContains(script, "generate_displacements(distance=delta)",
                      "phonopy generates the irreducible displacement set");
        checkContains(script, "produce_force_constants",
                      "force constants rebuilt by symmetry");
        checkContains(script, "displacements_irreducible",
                      "reports how many displacements survived the reduction");
        // The fallback is what keeps the script runnable without phonopy.
        checkContains(script, "except ImportError:", "guards the phonopy import");
        checkContains(script, "run_ase_displacements()",
                      "and falls back to the 6N driver");
        checkContains(script, "phonon_band.json",
                      "writes the same band schema as the ASE driver");
    }
    {
        PhononConfig p;
        p.removeResidualForces = true;
        const std::string script =
            PhononScriptGenerator::generate(p, "structure.extxyz");
        checkContains(script, "class ResidualFreeCalculator",
                      "residual-subtracting calculator is defined");
        checkContains(script, "residual = reference.get_forces()",
                      "baseline measured on the un-displaced supercell");
        checkContains(script, "CALANGO_INFO residual_fmax",
                      "residual magnitude is reported");
    }
    {
        // Molecules have no space group to exploit, but the residual removal
        // still applies — it is what keeps the 6 zero modes near zero.
        PhononConfig p;
        p.periodic = false;
        p.removeResidualForces = true;
        const std::string script =
            PhononScriptGenerator::generate(p, "structure.extxyz");
        checkContains(script, "class ResidualFreeCalculator",
                      "molecular path also subtracts residual forces");
        checkContains(script, "Vibrations", "still the normal-mode driver");
    }

    // -- Optics inherits a baseline; it never re-converges one ---------------
    std::printf("Optics baseline inheritance:\n");
    {
        OpticsConfig optics;
        optics.baselineDensityPath = "/jobs/proc_1/single_point.gpw";
        const std::string script = generateOpticsScript(optics);
        checkContains(script, "GPAW(r\"/jobs/proc_1/single_point.gpw\", txt=None)",
                      "loads the baseline ground state");
        checkContains(script, "gs.fixed_density(",
                      "evaluates the response at fixed density");
        checkContains(script,
                      "n_empty = max(int(round(n_occ * 200 / 100.0)), 12)",
                      "empty bands sized as a percentage of the occupied");
        // The whole point of the feature: a self-consistent cycle inside the
        // optics job would produce a spectrum from a DIFFERENT SCF solution
        // than the one the user inspected, with no visible sign of it.
        check(!contains(script, "atoms.get_potential_energy()"),
              "no self-consistent cycle is run");
        check(!contains(script, "twod_"),
              "a bulk run emits no 2D observables");
    }
    {
        OpticsConfig sheet;
        sheet.baselineDensityPath = "/jobs/proc_1/single_point.gpw";
        sheet.vacuumAxis = 2;
        const std::string script = generateOpticsScript(sheet);
        checkContains(script, "atoms.cell.lengths()[2]",
                      "reads the sheet thickness off the chosen vacuum axis");
        checkContains(script, "L_z / (4.0 * np.pi) * (eps1 - 1.0)",
                      "alpha_2D = L_z/(4 pi) (eps_3D - 1)");
        checkContains(script, "k = omega_eV / hbar_c_eV_A",
                      "photon wavevector from the energy");
        checkContains(script, "absorbance = k * L_z * eps2",
                      "A(omega) = (omega L_z / c) Im[eps_3D]");
        checkContains(script, "\"sigma_2D_re\"", "reports the 2D conductivity");
        // The values themselves are checked numerically against graphene's
        // universal absorbance by tests/optics_2d_test.py, which extracts this
        // function and runs it; here we only pin that it stays extractable.
        checkContains(script, "def twod_observables(omega_eV, eps1, eps2, L_z):",
                      "the observables live in an extractable function");
    }

    // -- Brillouin-zone integrator ------------------------------------------
    std::printf("Optics integration mode:\n");
    {
        OpticsConfig optics;
        optics.baselineDensityPath = "/jobs/proc_1/single_point.gpw";
        const std::string script = generateOpticsScript(optics);
        checkContains(script, "integrationmode = \"point integration\"",
                      "point integration is the default");
        checkContains(script, "\"integrationmode\": integrationmode",
                      "the integrator is recorded in the results");
        // Point integration keeps the literal evaluation: the response is
        // computed at exactly the requested frequencies, which costs more
        // than a transform and is what makes the window and point count mean
        // what they say. Only the tetrahedron path trades that away.
        checkContains(script, "_frequency_arg = frequencies_eV",
                      "and evaluates on the requested grid itself");
        checkContains(script, "_hilbert = False",
                      "with the transform off, which is what selects literal "
                      "evaluation");
        check(!contains(script, "\"type\": \"nonlinear\""),
              "and no non-linear descriptor is involved");
        checkContains(script, "omega_eV = frequencies",
                      "so the reporting grid is the evaluated one, unresampled");
    }
    {
        OpticsConfig optics;
        optics.baselineDensityPath = "/jobs/proc_1/single_point.gpw";
        optics.tetrahedronIntegration = true;
        const std::string script = generateOpticsScript(optics);
        // GPAW spells it with a SPACE, and the kwarg is `integrationmode`, not
        // `method` — both differ from the obvious guess.
        checkContains(script, "integrationmode = \"tetrahedron integration\"",
                      "tetrahedron mode uses GPAW's exact spelling");
        checkContains(script, "integrationmode=integrationmode",
                      "passed through to DielectricFunction");
        // The failure this guards: a k-mesh that does not reach the vertices
        // of the irreducible zone. GPAW tessellates the IBZ and keeps the
        // ground-state k-points inside it, so a mesh missing the vertices
        // leaves the tessellation with nothing to anchor on — the run used to
        // die partway through the first direction with a KeyError-shaped
        // failure deep inside the response module.
        //
        // It is now PREVENTED rather than diagnosed: the mesh is tested
        // against GPAW's own predicate before the expensive step and raised
        // to the smallest compliant grid.
        checkContains(script, "contains_ibz_vertices_predicate",
                      "the mesh is tested against GPAW's own IBZ-vertex "
                      "predicate");
        checkContains(script, "\"gamma\": True",
                      "and Gamma-centred — a shifted grid can never contain "
                      "Gamma, which is a vertex of every IBZ");
        checkContains(script, "kpts=_response_kpts_spec",
                      "the compliant mesh is what fixed_density receives");
        // What the first version of this got wrong, and why it shipped a
        // failing run: the predicate RAISES on an odd grid rather than
        // returning False for it, and it is vectorised — so a candidate list
        // stepping by one lost every candidate, including the even ones, to
        // that single exception. The mesh then went through unchanged and
        // GPAW rejected it after the NSCF had already been paid for.
        checkContains(script, "int(_n) + int(_n) % 2 if _pbc[_i]",
                      "periodic axes are rounded up to even before anything "
                      "else — GPAW raises on an odd grid rather than "
                      "rejecting it");
        checkContains(script, "range(int(_n), int(_n) + _span + 1, 2)",
                      "and candidates step by two, so no odd grid can reach "
                      "the predicate at all");
        checkContains(script, "_span = max(24, int(_n))",
                      "over a window wide enough for the sparse compliant "
                      "sizes: an fcc cell needs multiples of 8, so a request "
                      "of 9 is only satisfied at 16");
        checkContains(script, "_hits.extend(_compliant([_g]))",
                      "and a batch that still raises is retried grid by "
                      "grid, rather than one offender voiding the rest");
        checkContains(script, "CALANGO_WARN response k-mesh raised from",
                      "a substituted mesh is reported, not applied quietly");
        checkContains(script, "\"kpts_spec\"",
                      "and recorded alongside the spectrum, so the grid it "
                      "was computed on survives in the results");
        checkContains(script, "vertices of the IBZ",
                      "the late GPAW error is still detected");
        // The second half of the same failure: GPAW implements tetrahedron
        // integration ONLY as a Hilbert transform of the spectral function,
        // and that transform is defined on a non-linear frequency grid. An
        // explicit linear array dies either way — hilbert=False builds the
        // literal task whose update signature TetrahedronIntegrator does not
        // match, hilbert=True asserts on the descriptor type — so the window
        // is translated into that grid's parameters instead.
        checkContains(script, "\"type\": \"nonlinear\"",
                      "tetrahedron integration gets the non-linear frequency "
                      "descriptor its Hilbert transform requires");
        checkContains(script, "_hilbert = True",
                      "with the transform ON — there is no literal-evaluation "
                      "task for the tetrahedron integrator to use");
        check(!contains(script, "_hilbert = False"),
              "and never the flag that selects one");
        checkContains(script, "\"omega2\": 10.0",
                      "spacing doubles at GPAW's own default energy");
        checkContains(script, "float(frequencies_eV[-1]) * 1.5 + 5.0",
                      "and the grid runs past the requested window, because "
                      "eps_1 there is a Kramers-Kronig integral over the "
                      "weight above it");
        // The grid GPAW evaluates on is then NOT the one the user asked for,
        // so the spectra are brought back to it rather than the window
        // silently becoming whatever GPAW chose.
        checkContains(script, "omega_eV = frequencies_eV",
                      "the reporting grid stays the requested one");
        checkContains(script, "np.interp(omega_eV, frequencies, _e.real)",
                      "and the spectra are resampled onto it");
        checkContains(script, "eps = _on_report_grid(eps)",
                      "before any observable is derived from them");
        checkContains(script, "\"resampled\": bool(_resample)",
                      "with the fact recorded, so reported resolution is not "
                      "mistaken for computed resolution");
        // The remedy named in the message has to be an API that EXISTS:
        // find_high_symmetry_monkhorst_pack was removed from GPAW, so the old
        // text sent the user to a function they could not call.
        check(!contains(script, "find_high_symmetry_monkhorst_pack"),
              "and the message no longer names a GPAW function that has been "
              "removed");
        check(!contains(script, "integrationmode = \"point integration\""),
              "does not silently fall back to point integration");
    }

    // -- Response k-mesh and IBZ reduction -----------------------------------
    std::printf("Optics response sampling:\n");
    {
        OpticsConfig optics;
        optics.baselineDensityPath = "/jobs/proc_1/single_point.gpw";
        const std::string script = generateOpticsScript(optics);
        check(!contains(script, "kpts=_response_kpts_spec"),
              "no k-mesh line when every axis is left to the baseline");
        checkContains(script, "_response_kpts_spec = _response_kpts",
                      "and point integration passes the mesh through "
                      "unchanged — no Gamma-centring, no substitution");
        checkContains(script, "_requested_kpts = (0, 0, 0)",
                      "and the request is still reported for the log");
        checkContains(script, "symmetry=\"off\"",
                      "full-zone sampling by default");
    }
    {
        OpticsConfig optics;
        optics.baselineDensityPath = "/jobs/proc_1/single_point.gpw";
        optics.responseKpts[0] = 12;
        optics.responseKpts[1] = 12;
        optics.responseKpts[2] = 8;
        optics.includeIbzPoints = true;
        const std::string script = generateOpticsScript(optics);
        checkContains(script, "_requested_kpts = (12, 12, 8)",
                      "the denser response mesh overrides the baseline grid");
        // Leaving symmetry ON is what produces the weighted irreducible set;
        // emitting symmetry="off" alongside would cancel the whole feature.
        check(!contains(script, "symmetry=\"off\""),
              "IBZ mode does not also disable symmetry");
        checkContains(script, "get_k_point_weights()",
                      "reports the degeneracy weights it will integrate with");
        checkContains(script, "CALANGO_INFO response k-points=",
                      "reports how many points survived the reduction");
    }
    {
        // A partially specified mesh is a mesh with one axis left to the
        // baseline. This used to be discarded WHOLESALE on the reasoning that
        // 12x12x0 is not a valid grid — true, but the fix is to resolve the
        // zero rather than to throw the other two axes away. "24, 24, auto" is
        // the natural entry for a 2D sheet, and dropping it produced a
        // spectrum identical to not having set anything.
        OpticsConfig optics;
        optics.baselineDensityPath = "/jobs/proc_1/single_point.gpw";
        optics.responseKpts[0] = 12;
        optics.responseKpts[1] = 12;
        const std::string script = generateOpticsScript(optics);
        checkContains(script, "kpts=_response_kpts",
                      "a partially specified mesh is applied, not discarded");
        checkContains(script, "_requested_kpts = (12, 12, 0)",
                      "with the unset axis carried through");
        checkContains(script, "_baseline_kpts[_i]",
                      "and filled in from the baseline at run time");
    }

    // -- 2D work function: Φ = E_vac − E_F off an inherited ground state ------
    std::printf("2D work function:\n");
    {
        WorkfunctionConfig wf;
        wf.baselineDensityPath = "/jobs/proc_1/single_point.gpw";
        wf.vacuumAxis = 2;
        const std::string script = generateWorkfunctionScript(wf);
        checkContains(script, "GPAW(r\"/jobs/proc_1/single_point.gpw\", txt=None)",
                      "loads the baseline ground state");
        checkContains(script, "calc.get_electrostatic_potential()",
                      "reads the potential back rather than recomputing it");
        // The planar average and the subtraction ARE the physics: V(z) from
        // the mean over the two non-vacuum axes, Φ from E_vac − E_F.
        checkContains(script,
                      "in_plane = tuple(i for i in range(3) if i != vacuum_axis)",
                      "averages over exactly the two non-vacuum axes");
        checkContains(script, "v_planar = pot.mean(axis=in_plane)",
                      "planar-averages the potential");
        checkContains(script, "vacuum_axis = 2",
                      "the chosen vacuum axis is baked into the script");
        checkContains(script, "efermi = float(calc.get_fermi_level())",
                      "the Fermi level comes from the baseline");
        checkContains(script, "phi_low = v_vac_low - efermi",
                      "Φ is the vacuum level minus E_F (low face)");
        checkContains(script, "phi_high = v_vac_high - efermi",
                      "and the high face is reported separately");
        // A geometry post-process must not re-run the SCF, and it must read
        // the geometry off the .gpw — a workspace structure edited since the
        // baseline would put the vacuum bookkeeping on the wrong cell.
        check(!contains(script, "get_potential_energy"),
              "no self-consistent cycle is run");
        check(!contains(script, "structure.extxyz"),
              "the geometry comes from the .gpw, not the staged structure");
        // The workfunction.json schema the viewer parses, key by key: the
        // generator and WorkfunctionWindow never see each other in the build.
        for (const char* key :
             {"\"vacuum_axis\"", "\"z_A\"", "\"v_planar_eV\"", "\"efermi_eV\"",
              "\"vacuum_level_low_eV\"", "\"vacuum_level_high_eV\"",
              "\"workfunction_low_eV\"", "\"workfunction_high_eV\"",
              "\"plateau_flatness_eV_per_A\""})
            checkContains(script, key,
                          std::string("workfunction.json carries ") + key);
        checkContains(script, "plateau_fraction = 0.15",
                      "the default plateau window is 15% of the vacuum gap");
        checkContains(script, "if plateau_flatness > 0.005:",
                      "warns when the edge plateau is not flat");
        checkContains(script, "CALANGO_RESULT workfunction=workfunction.json",
                      "announces the result file");
    }
    {
        // The clamps: a stray axis or fraction must degrade to the documented
        // defaults, not to Python that averages over a non-existent axis.
        WorkfunctionConfig wf;
        wf.baselineDensityPath = "/jobs/proc_1/single_point.gpw";
        wf.vacuumAxis = 7;
        wf.plateauFraction = 3.0;
        const std::string script = generateWorkfunctionScript(wf);
        checkContains(script, "vacuum_axis = 2",
                      "an out-of-range axis falls back to z");
        checkContains(script, "plateau_fraction = 0.15",
                      "an out-of-range fraction falls back to the default");
    }
    {
        WorkfunctionConfig wf;
        wf.baselineDensityPath = "/jobs/proc_1/single_point.gpw";
        wf.vacuumAxis = 0;
        wf.plateauFraction = 0.25;
        const std::string script = generateWorkfunctionScript(wf);
        checkContains(script, "vacuum_axis = 0",
                      "a non-default vacuum axis is honored");
        checkContains(script, "plateau_fraction = 0.25",
                      "so is a non-default plateau fraction");
    }

    // -- VASP optics: the standard two-step LOPTICS protocol ------------------
    std::printf("VASP optics:\n");
    {
        OpticsConfig optics;
        optics.calculator.calculator = CalculatorKind::Vasp;
        optics.calculator.task = TaskKind::SinglePoint;
        optics.broadeningEv = 0.15;
        optics.npoints = 2400;
        const std::string script = generateOpticsScript(optics);
        // The recipe, tag by tag: SCF leaves the density, then an exact-
        // diagonalization restart at fixed density computes ε(ω).
        checkContains(script, "lwave=True, lcharg=True",
                      "the SCF step keeps CHGCAR and WAVECAR for the restart");
        checkContains(script, "icharg=11", "the optics step fixes the density");
        checkContains(script, "algo=\"Exact\"",
                      "exact diagonalization for a semilocal functional");
        checkContains(script, "nelm=1", "one diagonalization pass");
        checkContains(script, "loptics=True", "LOPTICS drives the response");
        checkContains(script, "cshift=0.15",
                      "the broadening maps onto CSHIFT");
        checkContains(script, "nedos=2400",
                      "the frequency-point count maps onto NEDOS");
        checkContains(script,
                      "n_empty = max(int(round(n_occ * 200 / 100.0)), 12)",
                      "empty bands sized as a percentage of the occupied");
        checkContains(script,
                      "params[\"nbands\"] = max(n_occ + n_empty, nbands_scf)",
                      "NBANDS = occupied + empty, never below the SCF's own");
        checkContains(script, "dielectricfunction",
                      "reads ε(ω) back from vasprun.xml");
        checkContains(script, "\"density\" in (candidate.get(\"comment\")",
                      "prefers the density-density block (VASP 6 writes two)");
        check(!contains(script, ".gpw"),
              "self-contained: no GPAW baseline is referenced");
        check(!contains(script, "twod_"),
              "a bulk run emits no 2D observables");
    }
    {
        // Exact exchange: the semilocal ALGO=Exact path does not apply the
        // exact-exchange operator to the new empty states — hybrids must
        // diagonalize with ALGO=Eigenval instead.
        OpticsConfig optics;
        optics.calculator.calculator = CalculatorKind::Vasp;
        optics.calculator.vaspXc = "HSE06";
        const std::string script = generateOpticsScript(optics);
        checkContains(script, "algo=\"Eigenval\"",
                      "a hybrid functional switches the restart to Eigenval");
        check(!contains(script, "algo=\"Exact\""),
              "and does not also emit the semilocal ALGO");
    }
    {
        // The 2D sheet variant and the denser optics mesh, VASP flavour.
        OpticsConfig sheet;
        sheet.calculator.calculator = CalculatorKind::Vasp;
        sheet.vacuumAxis = 2;
        sheet.calculator.kpts[0] = 6;
        sheet.calculator.kpts[1] = 6;
        sheet.calculator.kpts[2] = 1;
        sheet.responseKpts[0] = 18;
        sheet.responseKpts[1] = 18;
        const std::string script = generateOpticsScript(sheet);
        checkContains(script, "params[\"kpts\"] = (18, 18, 1)",
                      "the optics mesh densifies, unset axes inherit the SCF");
        checkContains(script, "def twod_observables(omega_eV, eps1, eps2, L_z):",
                      "the same sheet observables as the GPAW path");
        checkContains(script, "atoms.cell.lengths()[2]",
                      "thickness from the chosen vacuum axis");
    }

    // -- DFT+U and dispersion ------------------------------------------------
    std::printf("DFT+U and dispersion:\n");
    {
        CalculatorConfig c = gpawConfig();
        c.useHubbardU = true;
        c.hubbardU = {{"Fe", "d", 3.5, false}, {"Ni", "d", 4.6, false}};
        const std::string script =
            AseScriptGenerator::generate(c, "structure.extxyz");
        // The leading colon keeps the DEFAULT PAW dataset and appends the
        // correction; without it GPAW hunts for a differently named dataset.
        checkContains(script, "setups={\"Fe\": \":d,3.5\", \"Ni\": \":d,4.6\"}",
                      "emits GPAW's setups dictionary");
    }
    {
        CalculatorConfig c = gpawConfig();
        c.useHubbardU = false;
        c.hubbardU = {{"Fe", "d", 3.5, false}};
        check(!contains(AseScriptGenerator::generate(c, "structure.extxyz"),
                        "setups="),
              "a populated table stays unwritten while the toggle is off");
    }
    {
        CalculatorConfig c = gpawConfig();
        c.dispersionD4 = true;
        c.gpawXc = "PBEsol";
        const std::string script =
            AseScriptGenerator::generate(c, "structure.extxyz");
        checkContains(script, "from dftd4.ase import DFTD4",
                      "imports the dftd4 package's own calculator");
        checkContains(script, "from ase.calculators.mixing import SumCalculator",
                      "couples through ASE's SumCalculator");
        // The damping parameters are fitted per functional, so D4 must be told
        // which one it corrects — following the calculator's own xc.
        checkContains(
            script,
            "atoms.calc = SumCalculator([DFTD4(method=\"PBEsol\"), atoms.calc])",
            "sums D4 onto the calculator and follows its functional");
    }
    {
        // The coupling must apply to every calculator, not just GPAW.
        CalculatorConfig c = maceConfig();
        c.dispersionD4 = true;
        checkContains(AseScriptGenerator::generate(c, "structure.extxyz"),
                      "SumCalculator([DFTD4(method=\"PBE\"), atoms.calc])",
                      "couples D4 to a non-DFT calculator too");
    }
    {
        // vdW-corrected functionals (VV10, rVV10, the vdW-DF family) go
        // through libvdwxc's backend dict, not the plain xc string.
        CalculatorConfig c = gpawConfig();
        c.gpawXc = "rVV10";
        checkContains(AseScriptGenerator::generate(c, "structure.extxyz"),
                      "xc={\"name\": \"rVV10\", \"backend\": \"libvdwxc\"}",
                      "vdW functional selects the libvdwxc backend");
        c.gpawXc = "PBE";
        checkContains(AseScriptGenerator::generate(c, "structure.extxyz"),
                      "xc=\"PBE\"", "semilocal functionals keep the string form");
    }

    // -- GW quasiparticle pipelines -----------------------------------------
    std::printf("GW quasiparticle pipelines:\n");
    {
        GwConfig gw;
        gw.baselinePath = "/jobs/proc_1/single_point.gpw";
        gw.screeningCutoffEv = 150.0;
        const std::string script = generateGwScript(gw);
        checkContains(script, "from gpaw.response.g0w0 import G0W0",
                      "GPAW's own G0W0 driver");
        checkContains(script, "ecut=150", "screening cutoff is honored");
        checkContains(script, "ppa=True", "plasmon-pole treatment by default");
        checkContains(script, "gs.fixed_density(",
                      "empty bands are added at the baseline density");
        check(!contains(script, "p2y"), "no Yambo steps in the GPAW pipeline");
        checkContains(script, "gap_renormalization_eV",
                      "reports the gap renormalization");
    }
    {
        GwConfig gw;
        gw.baselinePath = "/jobs/proc_1/single_point.gpw";
        gw.frequency = GwFrequencyTreatment::RealAxis;
        checkContains(generateGwScript(gw), "ppa=False",
                      "real-axis treatment switches off the plasmon pole");
    }
    {
        GwConfig yambo;
        yambo.engine = GwEngine::Yambo;
        yambo.baselinePath = "/jobs/proc_2";
        yambo.yamboCores = 8;
        const std::string script = generateGwScript(yambo);
        checkContains(script, "\"p2y\"", "converts the QE save to a Yambo database");
        checkContains(script, "\"-g\", \"n\", \"-p\", \"p\"",
                      "generates a plasmon-pole G0W0 input");
        checkContains(script, "o-gw.qp", "parses the quasiparticle report");
        checkContains(script, "mpirun", "honors the MPI rank count");
        check(!contains(script, "from gpaw"),
              "no GPAW imports in the Yambo pipeline");
        // A silent failure is worse than a loud one here: without the abort,
        // the parser would read the PREVIOUS run's report and hand back
        // confident, stale quasiparticle energies.
        checkContains(script, "raise RuntimeError(", "a failed step aborts the run");
    }
    {
        GwConfig yambo;
        yambo.engine = GwEngine::Yambo;
        yambo.frequency = GwFrequencyTreatment::RealAxis;
        checkContains(generateGwScript(yambo), "\"-g\", \"n\", \"-p\", \"r\"",
                      "real-axis selects the -p r screening form");
    }

    // -- The ASE-backed engines added on top of the original set -------------
    //
    // Every one of these builds a REAL ase.calculators.* object, which is what
    // lets the ASE optimizers / MD / vibrations drive them without any of them
    // being special-cased. The checks below pin the import and the two or three
    // parameters per engine whose meaning is easy to get wrong — in every case
    // a mistake that produces a script which RUNS and answers a different
    // question than the one asked.
    std::printf("ABINIT:\n");
    {
        CalculatorConfig abinit;
        abinit.calculator = CalculatorKind::Abinit;
        abinit.planeWaveCutoffEv = 600.0;
        abinit.abinitPps = "paw";
        abinit.abinitPseudoDir = "/opt/abinit/jth";
        const std::string script =
            AseScriptGenerator::generate(abinit, "structure.extxyz");
        checkContains(script, "from ase.calculators.abinit import Abinit",
                      "builds ASE's Abinit calculator");
        // ecut in eV: ASE converts to Hartree itself, so the shared cutoff
        // passes through unscaled. Converting here too would divide by 27.2
        // twice and quietly run a 22 eV calculation.
        checkContains(script, "ecut=600", "passes the cutoff in eV, unconverted");
        checkContains(script, "pps=\"paw\"", "names the pseudopotential family");
        checkContains(script, "/opt/abinit/jth", "points at the table directory");
    }

    std::printf("FHI-aims:\n");
    {
        CalculatorConfig aims;
        aims.calculator = CalculatorKind::FhiAims;
        aims.aimsSpeciesDir = "/opt/aims/species_defaults";
        aims.aimsSpeciesTier = "tight";
        const std::string script =
            AseScriptGenerator::generate(aims, "structure.extxyz");
        checkContains(script, "from ase.calculators.aims import Aims",
                      "builds ASE's Aims calculator");
        checkContains(script, "\"tight\"", "joins the species tier onto the dir");
        checkContains(script, "relativistic=\"atomic_zora scalar\"",
                      "defaults to the scalar-relativistic treatment");
        // Modern ASE's Aims is a GenericFileIOCalculator: calling .set(...)
        // AFTER construction — the pattern every other engine block here
        // uses, and what this generator used to do — raises unconditionally
        // ("No setting parameters for now, please. Just create new
        // calculators."), confirmed against the real installed ase 3.29.
        // EVERY parameter (k_grid included, despite depending on whether the
        // structure is periodic) has to reach the ORIGINAL constructor call.
        checkNotContains(script, "atoms.calc.set(",
                         "no post-construction .set() call anywhere — aims's "
                         "GenericFileIOCalculator raises RuntimeError the "
                         "instant one is attempted");
        checkContains(script, "if any(atoms.pbc):\n"
                              "    aims_kwargs[\"k_grid\"]",
                      "k_grid is conditional (aims rejects one on a "
                      "non-periodic system) but still lands in the kwargs "
                      "dict BEFORE Aims(...) is constructed");
        checkContains(script, "atoms.calc = Aims(\n"
                              "    profile=AimsProfile(command=os.environ.get(\n"
                              "        \"ASE_AIMS_COMMAND\", \"aims.x\")),\n"
                              "    **aims_kwargs,\n"
                              ")\n",
                      "exactly one Aims(...) call, built from the "
                      "accumulated kwargs");
        // The basis is the species tier; there is no plane-wave cutoff to set,
        // and emitting one would be a keyword aims does not have.
        check(!contains(script, "ecut"), "emits no plane-wave cutoff");
        checkContains(script, "profile=AimsProfile(command=os.environ.get(",
                      "the launch command is resolved through AimsProfile, "
                      "ASE's GenericFileIOCalculator profile — not a bare "
                      "command= kwarg the calculator no longer accepts");
        checkContains(script, "xc=\"pbe\"", "the XC functional is written");
        checkContains(script, "aims_kwargs[\"k_grid\"] = (7, 7, 7)",
                      "translated onto aims's own k_grid keyword");
        checkContains(script, "sc_accuracy_etot=1e-06",
                      "the SCF energy-convergence target reaches aims");

        // Smearing: the DEFAULT SmearingMethod is Fermi-Dirac, and until this
        // was fixed the generated occupation_type was hard-coded to
        // "gaussian" regardless of the method actually selected — so every
        // aims run silently computed a Gaussian-smeared density instead.
        checkContains(script, "aims_kwargs[\"occupation_type\"] = \"fermi 0.1\"",
                      "Fermi-Dirac (the default) reaches aims's own name for "
                      "it, not a fixed \"gaussian\"");

        CalculatorConfig aimsGaussian = aims;
        aimsGaussian.smearing = SmearingMethod::Gaussian;
        aimsGaussian.smearingWidthEv = 0.2;
        checkContains(AseScriptGenerator::generate(aimsGaussian, "structure.extxyz"),
                      "aims_kwargs[\"occupation_type\"] = \"gaussian 0.2\"",
                      "Gaussian smearing keeps its own name and width");

        CalculatorConfig aimsMp = aims;
        aimsMp.smearing = SmearingMethod::MethfesselPaxton;
        aimsMp.smearingWidthEv = 0.15;
        aimsMp.smearingOrder = 2;
        checkContains(AseScriptGenerator::generate(aimsMp, "structure.extxyz"),
                      "aims_kwargs[\"occupation_type\"] = "
                      "\"methfessel-paxton 0.15 2\"",
                      "Methfessel-Paxton carries its order AFTER the width — "
                      "aims's argument order, not GPAW's");

        CalculatorConfig aimsCold = aims;
        aimsCold.smearing = SmearingMethod::MarzariVanderbilt;
        checkContains(AseScriptGenerator::generate(aimsCold, "structure.extxyz"),
                      "aims_kwargs[\"occupation_type\"] = \"cold ",
                      "Marzari-Vanderbilt is aims's \"cold\" smearing, not "
                      "\"marzari-vanderbilt\" (aims has no such keyword)");

        // Spin.
        CalculatorConfig aimsSpin = aims;
        aimsSpin.spinPolarized = true;
        aimsSpin.spinMode = SpinMode::Collinear;
        aimsSpin.initialMagMoment = 2.5;
        const std::string aimsSpinScript =
            AseScriptGenerator::generate(aimsSpin, "structure.extxyz");
        checkContains(aimsSpinScript, "aims_kwargs[\"spin\"] = \"collinear\"",
                      "spin polarization reaches aims through the kwargs dict");
        checkContains(aimsSpinScript,
                      "aims_kwargs[\"default_initial_moment\"] = 2.5",
                      "and so does the seed moment");
        checkNotContains(aimsSpinScript, "atoms.calc.set(",
                         "still no post-construction .set() with spin on");

        // The AIMS_SPECIES_DIR environment fallback, when no directory is
        // configured in Preferences.
        CalculatorConfig noSpecies = aims;
        noSpecies.aimsSpeciesDir.clear();
        checkContains(AseScriptGenerator::generate(noSpecies, "structure.extxyz"),
                      "os.environ.get(\"AIMS_SPECIES_DIR\"",
                      "an unconfigured species_defaults directory falls back "
                      "to the environment variable rather than a silent "
                      "empty path aims would then fail to resolve obscurely");

        // The free-form escape hatch.
        CalculatorConfig aimsExtra = aims;
        aimsExtra.aimsExtra = "charge_mix_param 0.3";
        checkContains(AseScriptGenerator::generate(aimsExtra, "structure.extxyz"),
                      "aims_kwargs[\"charge_mix_param\"] = 0.3",
                      "extra control.in keywords reach the kwargs dict too");
    }

    std::printf("NWChem — the molecular / periodic split:\n");
    {
        CalculatorConfig molecular;
        molecular.calculator = CalculatorKind::NwChem;
        molecular.nwchemTheory = "dft";
        molecular.nwchemBasis = "def2-TZVP";
        const std::string script =
            AseScriptGenerator::generate(molecular, "structure.extxyz");
        checkContains(script, "theory=\"dft\"", "selects the molecular module");
        checkContains(script, "basis=\"def2-TZVP\"",
                      "and gives it a Gaussian basis");
        check(!contains(script, "kpts="),
              "a molecular module gets no k-points (it ignores the cell)");
    }
    {
        CalculatorConfig periodic;
        periodic.calculator = CalculatorKind::NwChem;
        periodic.nwchemTheory = "band";
        periodic.kpts[0] = 4;
        periodic.kpts[1] = 4;
        periodic.kpts[2] = 4;
        const std::string script =
            AseScriptGenerator::generate(periodic, "structure.extxyz");
        checkContains(script, "kpts=(4, 4, 4)",
                      "a plane-wave module gets the k-grid");
        check(!contains(script, "basis="),
              "and no Gaussian basis, which it does not read");
    }

    std::printf("OpenMX:\n");
    {
        CalculatorConfig openmx;
        openmx.calculator = CalculatorKind::OpenMx;
        openmx.openmxDataPath = "/opt/openmx/DFT_DATA19";
        openmx.openmxEigenSolver = "Band";
        const std::string script =
            AseScriptGenerator::generate(openmx, "structure.extxyz");
        checkContains(script, "from ase.calculators.openmx import OpenMX",
                      "builds ASE's OpenMX calculator");
        checkContains(script, "OPENMX_DFT_DATA_PATH", "exports the data path");
        checkContains(script, "eigensolver=\"Band\"", "selects the solver");
        // The grid cutoff must not be presented as a basis cutoff — that
        // mislabel is what made SIESTA's mesh look like a convergence knob.
        checkContains(script, "the REAL-SPACE grid",
                      "says the energy cutoff is a grid, not a basis");
    }

    std::printf("FLEUR:\n");
    {
        CalculatorConfig fleur;
        fleur.calculator = CalculatorKind::Fleur;
        fleur.fleurKmax = 4.5;
        const std::string script =
            AseScriptGenerator::generate(fleur, "structure.extxyz");
        checkContains(script, "from ase_fleur import",
                      "imports the ase-fleur package");
        checkContains(script, "pip install ase-fleur",
                      "and says what to install when it is missing");
        checkContains(script, "kmax=4.5", "passes K_max");
    }

    std::printf("CP2K — the cutoff that is a grid, not a basis:\n");
    {
        CalculatorConfig cp2k;
        cp2k.calculator = CalculatorKind::Cp2k;
        cp2k.cp2kBasisSet = "TZVP-MOLOPT-GTH";
        cp2k.relaxCell = true;
        cp2k.task = TaskKind::GeometryOptimization;
        const std::string script =
            AseScriptGenerator::generate(cp2k, "structure.extxyz");
        checkContains(script, "from ase.calculators.cp2k import CP2K",
                      "builds ASE's CP2K calculator");
        checkContains(script, "basis_set=\"TZVP-MOLOPT-GTH\"",
                      "the BASIS is the Gaussian set");
        checkContains(script, "REL_CUTOFF", "emits the multi-grid rel. cutoff");
        // Without STRESS_TENSOR the cell filter sees zeros and the lattice
        // never moves, while the run reports a converged variable-cell
        // relaxation.
        checkContains(script, "stress_tensor=True",
                      "a variable-cell run asks CP2K for the stress");
    }
    {
        CalculatorConfig cp2k;
        cp2k.calculator = CalculatorKind::Cp2k;
        checkContains(AseScriptGenerator::generate(cp2k, "structure.extxyz"),
                      "stress_tensor=False",
                      "a fixed-cell run does not pay for it");
    }

    std::printf("Amber:\n");
    {
        CalculatorConfig amber;
        amber.calculator = CalculatorKind::Amber;
        amber.amberTopologyFile = "/data/system.prmtop";
        const std::string script =
            AseScriptGenerator::generate(amber, "structure.extxyz");
        checkContains(script, "from ase.calculators.amber import Amber",
                      "builds ASE's Amber calculator");
        checkContains(script, "/data/system.prmtop", "uses the given topology");
        // sander's own default is a MINIMIZATION: with imin=1 every force
        // evaluation ASE asks for would be a complete relaxation, and the
        // reported trajectory would be a sequence of already-relaxed frames.
        checkContains(script, "imin=0, nstlim=0",
                      "the generated mdin is a single-point, not a minimization");
    }
    {
        CalculatorConfig amber;
        amber.calculator = CalculatorKind::Amber;
        checkContains(AseScriptGenerator::generate(amber, "structure.extxyz"),
                      "raise RuntimeError(",
                      "refuses to run without a topology (there is no force "
                      "field without one)");
    }

    // -- Cluster expansion: variable-cell relaxation of every configuration --
    //
    // A hull built from fixed-cell energies is not comparable to one built
    // from relaxed-cell energies, so the batch has to offer the same filters
    // and masks the standalone Geometry Optimization module does — and the
    // optimizer has to be handed the FILTER, not the bare atoms, or the cell
    // silently never moves while the wizard claims it did.
    std::printf("Cluster expansion — variable cell:\n");
    {
        ClusterExpansionRunConfig batch;
        batch.calculator = maceConfig();
        batch.calculator.optimizer = Optimizer::LBFGS;
        batch.calculator.relaxCell = true;
        batch.calculator.cellFilter = CellFilter::FrechetCell;
        const std::string script =
            ClusterExpansionScriptGenerator::generate(batch);
        checkContains(script, "from ase.filters import FrechetCellFilter",
                      "imports the selected cell filter");
        checkContains(script, "from ase.constraints import UnitCellFilter",
                      "falls back for ASE < 3.23");
        checkContains(script, "_target = _CellFilter(atoms, "
                              "hydrostatic_strain=False)",
                      "wraps each configuration in the filter");
        checkContains(script, "LBFGS(_target,",
                      "the optimizer drives the FILTER, not the bare atoms");
        checkContains(script, "\"relax_cell\": relax_cell",
                      "records in the JSON that cells were relaxed");
        checkContains(script, "record[\"volume\"]",
                      "records each relaxed volume");
    }
    {
        // The 2Dxy / custom preset writes an explicit Voigt mask, in the same
        // [xx, yy, zz, yz, xz, xy] order the relaxation script uses.
        ClusterExpansionRunConfig batch;
        batch.calculator = maceConfig();
        batch.calculator.relaxCell = true;
        batch.calculator.cellCustomMask = true;
        batch.calculator.cellMask[2] = false;
        batch.calculator.cellMask[3] = false;
        batch.calculator.cellMask[4] = false;
        checkContains(ClusterExpansionScriptGenerator::generate(batch),
                      "_CellFilter(atoms, mask=[1, 1, 0, 0, 0, 1])",
                      "the 2Dxy Voigt mask reaches the generated script");
    }
    {
        // A single-point pass takes no steps, so no filter may be emitted —
        // an unused import is harmless, but `_CellFilter` referenced where no
        // optimizer runs is a NameError at line 1 of a 200-configuration job.
        ClusterExpansionRunConfig batch;
        batch.calculator = maceConfig();
        batch.calculator.relaxCell = true;
        batch.singlePointOnly = true;
        const std::string script =
            ClusterExpansionScriptGenerator::generate(batch);
        check(!contains(script, "_CellFilter"),
              "a single-point batch emits no cell filter");
        checkContains(script, "relax_cell = False",
                      "and says so in the run header");
    }

    // -- CALPHAD: the one script that imports pycalphad ----------------------
    //
    // pycalphad is installed in NO Calango environment, and nothing in the
    // application may import it at load time — that constraint is why the .tdb
    // parser, the Redlich-Kister fit and the phase diagrams are hand-written
    // C++. This script is the single exception, and because it can never be
    // run in CI, what it does WHEN THE IMPORT FAILS is the only part of it any
    // test can reach.
    std::printf("CALPHAD equilibrium script:\n");
    {
        CalphadScriptConfig config;
        config.components = {"AL", "ZN"};
        config.phases = {"FCC_A1", "LIQUID"};
        config.axisElement = "ZN";
        const std::string script = CalphadScriptGenerator::generate(config);

        // The import is guarded, and the guard says what to type.
        checkContains(script, "except ImportError",
                      "the pycalphad import is guarded");
        checkContains(script, "pip install pycalphad",
                      "and the failure names the command that fixes it");
        checkContains(script, "raise SystemExit(2)",
                      "exiting non-zero rather than continuing without a "
                      "solver");
        checkContains(script, "_calango_event(\"error\"",
                      "and recording the refusal in log.json, which is what "
                      "the Results panel reads");
        // The import must be INSIDE the try. A module-level pycalphad import
        // would make the script die with a traceback before the logger it
        // needs to report the failure through even exists.
        check(script.find("try:\n    from pycalphad") != std::string::npos,
              "the import sits inside the try, not at module level");

        // VA is appended for the user. Without it pycalphad rejects any
        // database whose sublattice model names a vacancy, with a message
        // about components that points nowhere near the cause.
        checkContains(script, "\"VA\"", "the vacancy joins the component list");
        checkNotContains(script, "import calango",
                         "and the script imports nothing from Calango");
        checkContains(script, "def _calango_progress(",
                      "carrying its own logger, like every generated script");

        CalphadScriptConfig ternary = config;
        ternary.ternary = true;
        ternary.components = {"AL", "MG", "ZN"};
        ternary.secondAxisElement = "MG";
        const std::string section = CalphadScriptGenerator::generate(ternary);
        checkContains(section, "ternary_isothermal_section",
                      "the ternary branch reports its own kind");
        // Both branches index the equilibrium grid positionally, so the
        // dimension order has to be pinned rather than assumed: pycalphad has
        // changed it between releases, and a silent transpose produces a
        // diagram that is mirrored about its own diagonal.
        checkContains(section, ".transpose(",
                      "with the dimension order pinned explicitly");
        checkContains(script, ".transpose(",
                      "and so does the binary branch");
    }

    std::printf("Thermodynamic integration:\n");
    {
        TiRunConfig ti;
        ti.calculator = maceConfig();
        ti.calculator.task = TaskKind::MolecularDynamics;
        ti.windows = 8;
        ti.equilibrationSteps = 1500;
        ti.productionSteps = 7000;
        ti.resultsDir = "/tmp/calango_ti";
        const std::string script =
            ThermodynamicIntegrationScriptGenerator::generate(ti);

        checkNotContains(script, "import calango",
                         "the script imports nothing from Calango");
        checkContains(script, "def _calango_progress(",
                      "carrying its own embedded logger");
        // The engine must arrive through the SHARED calculator block, not
        // through a second engine table of this module's own.
        checkContains(script, "mace_mp(",
                      "the target Hamiltonian is the wizard's engine");
        // Equilibration and production must be two separate dyn.run() calls
        // with the sampler attached only between them. A single run with the
        // sampler attached from step 0 averages over the transient, which
        // biases every window in the same direction and therefore survives the
        // lambda integral instead of cancelling.
        check(script.find("dyn.run(EQUILIBRATION_STEPS)")
                  < script.find("dyn.attach(_ti_record"),
              "equilibration runs BEFORE the sampler is attached");
        check(script.find("dyn.attach(_ti_record")
                  < script.find("dyn.run(PRODUCTION_STEPS)"),
              "and production runs after it");
        checkContains(script, "EQUILIBRATION_STEPS = 1500",
                      "the equilibration length reaches the script");
        checkContains(script, "PRODUCTION_STEPS = 7000",
                      "and so does the production length");

        // Gauss-Legendre is the default schedule precisely because its nodes
        // are strictly interior — lambda = 0 against an ideal gas is the
        // endpoint singularity.
        checkNotContains(script, "LAMBDAS = [0,",
                         "the default path never samples lambda = 0");
        checkContains(script, "\"quadrature\": QUADRATURE",
                      "the quadrature rule travels with the results, so the "
                      "reader never has to guess which weights are valid");
        checkContains(script, "series_eV",
                      "the raw dU/dlambda series is written, not just its mean");
        checkContains(script, "SWEEPS = [\"forward\"]",
                      "one sweep unless hysteresis was asked for");

        // THE LAMBDA NODES MUST SURVIVE THE ROUND TRIP EXACTLY.
        //
        // Gauss-Legendre weights are valid only on the Gauss-Legendre nodes,
        // and quadratureWeights() checks that before using them. Written at
        // ostringstream's default six significant digits, every node misses
        // the exact one by ~1e-9, the check fails, and the run is silently
        // re-integrated with a trapezoid on a grid chosen for Gauss — an
        // accuracy loss with nothing in the output pointing at the cause. So
        // the lambdas are parsed straight back out of the emitted text here
        // and fed to the rule that will judge them.
        {
            const std::size_t begin = script.find("LAMBDAS = [");
            check(begin != std::string::npos, "the script declares LAMBDAS");
            const std::size_t open = script.find('[', begin);
            const std::size_t close = script.find(']', open);
            std::vector<double> parsed;
            std::size_t cursor = open + 1;
            while (cursor < close) {
                std::size_t next = script.find(',', cursor);
                if (next == std::string::npos || next > close)
                    next = close;
                double value = 0.0;
                if (localeSafeParse(
                        std::string_view(script).substr(cursor, next - cursor),
                        &value))
                    parsed.push_back(value);
                cursor = next + 1;
            }
            check(parsed.size() == static_cast<std::size_t>(ti.windows),
                  "and every node parses back out of it");
            const TiQuadratureWeights weights =
                quadratureWeights(TiQuadrature::GaussLegendre, parsed);
            check(weights.ruleUsed == TiQuadrature::GaussLegendre
                      && weights.note.empty(),
                  "the emitted nodes are still recognised as Gauss-Legendre "
                  "ones after a text round trip");
        }

        TiRunConfig einstein = ti;
        einstein.reference = TiReference::EinsteinCrystal;
        einstein.hysteresis = true;
        const std::string solid =
            ThermodynamicIntegrationScriptGenerator::generate(einstein);
        checkContains(solid, "class _TiEinsteinCrystal",
                      "the Einstein reference brings its own calculator");
        checkContains(solid, "FixCom",
                      "and holds the centre of mass, which the closed-form "
                      "reference is corrected for");
        checkContains(solid, "SWEEPS = [\"forward\", \"backward\"]",
                      "hysteresis adds the reverse sweep");

        // A job that owns a SLICE cannot run a hysteresis sweep: there is no
        // sequential chain of windows to reverse. It must be withdrawn rather
        // than producing two independent estimates of the same average and
        // calling their difference hysteresis.
        TiRunConfig slice = einstein;
        slice.windowIndices = {2, 3};
        const std::string partial =
            ThermodynamicIntegrationScriptGenerator::generate(slice);
        checkContains(partial, "WINDOW_INDICES = [2, 3]",
                      "a split job owns only its own windows");
        checkContains(partial, "SWEEPS = [\"forward\"]",
                      "and the hysteresis sweep is withdrawn for it");
        // The whole path is still declared, so the aggregation knows what a
        // complete set looks like and can say which windows are missing. The
        // check is that the slice's LAMBDAS line is IDENTICAL to the full
        // run's: a job that renumbered the path to its own two windows would
        // write ti_window_000 and ti_window_001 and look complete.
        const auto lambdaLine = [](const std::string& text) {
            const std::size_t begin = text.find("LAMBDAS = [");
            return begin == std::string::npos
                ? std::string()
                : text.substr(begin, text.find('\n', begin) - begin);
        };
        check(!lambdaLine(partial).empty()
                  && lambdaLine(partial) == lambdaLine(solid),
              "while declaring exactly the same lambda path as the full run");

        // Splitting.
        const auto slices =
            ThermodynamicIntegrationScriptGenerator::splitWindows(12, 5);
        check(slices.size() == 5, "12 windows split into 5 jobs");
        std::size_t total = 0;
        std::size_t largest = 0;
        std::size_t smallest = 12;
        for (const auto& part : slices) {
            total += part.size();
            largest = std::max(largest, part.size());
            smallest = std::min(smallest, part.size());
        }
        check(total == 12, "covering every window exactly once");
        check(largest - smallest <= 1,
              "as evenly as possible — the run costs the slowest job");
    }

    std::printf("Piezoelectric tensor:\n");
    {
        auto baseConfig = []() {
            PiezoelectricConfig cfg;
            cfg.calculator = gpawConfig();
            cfg.baselinePath = "/jobs/proc_1/single_point.gpw";
            cfg.strainMagnitude = 0.01;
            cfg.voigtComponents = {0}; // xx only, for a small script
            return cfg;
        };

        // A non-GPAW engine is refused before any strained SCF is
        // generated — same rationale, and same shape of message, as Born
        // Charges: this method only exists through GPAW's Berry-phase
        // module today.
        PiezoelectricConfig notGpaw = baseConfig();
        notGpaw.calculator.calculator = CalculatorKind::Vasp;
        const std::string refused = generatePiezoelectricScript(notGpaw);
        checkContains(refused, "raise RuntimeError",
                      "a non-GPAW engine fails immediately");
        checkContains(refused, "Berry-phase",
                      "and the message names why");
        checkNotContains(refused, "STRAIN_POINTS",
                         "without ever building the strain stencil");

        const std::string script = generatePiezoelectricScript(baseConfig());
        checkContains(script, "from gpaw.berryphase import polarization_phase",
                      "reuses the shared GPAW Berry-phase evaluation");
        checkContains(script, "get_polarization_phase",
                      "with the pre-25.x fallback intact");
        checkContains(script, "result['phase_c']",
                      "and reads the total (electronic + ionic) phase");
        // The exact F entry for the xx component at delta = 0.01: F_00 =
        // 1 + eps = 1.01. This is the one number StrainVoigtTest.cpp proves
        // correct in isolation; this assertion is what proves the SAME
        // number reaches the generated script rather than a re-derived
        // (and possibly diverging) copy.
        checkContains(script, "\"F\": [[1.01, 0, 0]",
                      "the precomputed deformation gradient for eps=+0.01 "
                      "on Voigt 0 (xx) reaches the script verbatim");
        checkContains(script, "\"voigt\": 0, \"eps\": -0.01",
                      "and its -delta partner is also generated");
        checkContains(script, "scale_atoms=True",
                      "clamped-ion: fractional coordinates stay fixed under "
                      "the strain");
        checkContains(script, "np.unwrap(phases, axis=0)",
                      "branch fix: the multivalued Berry phase is resolved "
                      "onto a continuous series before differencing");
        checkContains(script, "d_jk * P0[i]",
                      "assembles the proper/improper correction "
                      "(Vanderbilt Eq. 15)");
        checkContains(script, "CALANGO_RESULT piezoelectric=piezoelectric.json",
                      "emits the marker the controller watches for");

        // Symmetry: on by default, with the centrosymmetric short-circuit
        // present; off, neither spglib nor the refusal is emitted at all.
        checkContains(script, "import spglib",
                      "symmetry detection is on by default");
        checkContains(script, "CENTROSYMMETRIC",
                      "and refuses outright for a centrosymmetric point "
                      "group, before spending any compute");
        PiezoelectricConfig noSymmetry = baseConfig();
        noSymmetry.useSymmetry = false;
        const std::string withoutSymmetry = generatePiezoelectricScript(noSymmetry);
        checkNotContains(withoutSymmetry, "import spglib",
                         "symmetry can be turned off entirely");

        // Clamped-ion is the base case; relaxed-ion swaps in a geometry
        // optimization of the internal coordinates at the fixed, strained
        // cell before the polarization is read off.
        // apply_strain() ALWAYS sets the clamped-ion (scale_atoms=True)
        // starting positions — relaxed-ion then optionally minimizes from
        // there, so "CLAMPED-ION" legitimately appears in both scripts; what
        // is mutually exclusive is whether strained_phase() stops at that
        // bare SCF or goes on to relax it.
        checkContains(script, "strained.get_potential_energy()",
                      "clamped-ion evaluates the SCF at the strain-scaled "
                      "positions with no further relaxation");
        checkNotContains(script, "LBFGS",
                         "and no relaxation runs unless asked for");
        PiezoelectricConfig relaxed = baseConfig();
        relaxed.relaxIons = true;
        const std::string relaxedScript = generatePiezoelectricScript(relaxed);
        checkContains(relaxedScript, "RELAXED-ION",
                      "relaxed-ion is offered as an explicit opt-in");
        checkContains(relaxedScript, "LBFGS(strained",
                      "which relaxes positions at the strained (fixed) cell");
        checkNotContains(relaxedScript, "strained.get_potential_energy()",
                         "not both conventions' terminal SCF call in the "
                         "same script — LBFGS drives the energy calls "
                         "itself");

        // More points per component: a 4-point stencil doubles the sample
        // count for the one requested component, all still Calango-side
        // precomputed deformation gradients.
        PiezoelectricConfig fourPoint = baseConfig();
        fourPoint.pointsPerComponent = 4;
        const std::string fourPointScript = generatePiezoelectricScript(fourPoint);
        std::size_t voigtZeroCount = 0;
        std::size_t pos = 0;
        while ((pos = fourPointScript.find("\"voigt\": 0,", pos)) != std::string::npos) {
            ++voigtZeroCount;
            pos += 1;
        }
        check(voigtZeroCount == 4,
              "the 4-point option samples +-delta and +-2*delta for the "
              "requested component");

        // The e -> d conversion is emitted only when an elastic stiffness
        // tensor is supplied — and is a clean no-op (None) otherwise, never
        // a silently wrong zero tensor.
        checkContains(script, "d_tensor_pmV = None",
                      "no elastic stiffness supplied: d_ij is explicitly "
                      "absent, not a wrong zero");
        PiezoelectricConfig withElastic = baseConfig();
        std::array<std::array<double, 6>, 6> stiffness{};
        for (int i = 0; i < 6; ++i)
            stiffness[static_cast<std::size_t>(i)][static_cast<std::size_t>(i)] = 300.0; // GPa
        withElastic.elasticStiffnessGpa = stiffness;
        const std::string withD = generatePiezoelectricScript(withElastic);
        checkContains(withD, "S_compliance_per_GPa = np.linalg.inv",
                      "with a stiffness tensor supplied, S = C^-1 is computed");
        checkContains(withD, "d_tensor_pmV = (proper_symmetrized @ "
                             "S_compliance_per_GPa) * 1e3",
                      "and d = e . S is reported in the conventional pm/V");
    }

    std::printf(failures == 0 ? "\nAll script checks passed.\n"
                              : "\n%d check(s) FAILED.\n",
                failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
