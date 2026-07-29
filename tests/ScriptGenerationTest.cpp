// Generated-ASE-script test.
//
// Covers the two things that can silently break a run long after the wizard
// closed: (1) the script must import the logger from the staged calango_log
// module rather than carrying its own inline copy, and (2) the calculator
// blocks must spell out the parameters the wizard collected (MACE precision /
// weights file / device; GPAW mode, xc, eigensolver, mixer, convergence).
//
// GUI-free and Python-free. With `--dump <dir>` it writes each generated
// script to disk instead of asserting, so a shell step can byte-compile them
// against a real ASE install (see the accompanying check in the repo docs).

#include "core/AseScriptGenerator.hpp"
#include "core/BornChargesScriptGenerator.hpp"
#include "core/CddScriptGenerator.hpp"
#include "core/UnfoldingScriptGenerator.hpp"
#include "core/XasScriptGenerator.hpp"
#include "core/ElectronicScriptGenerator.hpp"
#include "core/GwScriptGenerator.hpp"
#include "core/OpticsScriptGenerator.hpp"
#include "core/PhononScriptGenerator.hpp"
#include "core/RamanIrScriptGenerator.hpp"
#include "core/RandomNoiseScriptGenerator.hpp"
#include "core/DefectScriptGenerator.hpp"
#include "core/FermiSurfaceScriptGenerator.hpp"
#include "core/TopologyScriptGenerator.hpp"
#include "core/TwoDBandsScriptGenerator.hpp"
#include "core/WannierScriptGenerator.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

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
        CalculatorConfig mace = maceConfig();
        mace.macePrecision = MacePrecision::Float32;
        mace.maceDevice = "mps";
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
        }

        std::ofstream module(dir + "/"
                             + AseScriptGenerator::loggerModuleFileName());
        module << AseScriptGenerator::loggerModuleSource();
        std::printf("scripts written to %s\n", dir.c_str());
        return EXIT_SUCCESS;
    }

    // -- The logger is imported, never inlined ------------------------------
    std::printf("Logger module refactoring:\n");
    {
        const std::string script =
            AseScriptGenerator::generate(CalculatorConfig{}, "structure.extxyz");
        checkContains(script, "from calango_log import CalangoLog",
                      "generated script imports the module");
        checkContains(script, "_calango_log = CalangoLog()",
                      "and instantiates it under the historical name");
        // The whole point of the refactor: the class body must be gone.
        check(!contains(script, "class _CalangoLog"),
              "no inline logger class definition remains");
        check(!contains(script, "_json.dump"),
              "no inline JSON flushing remains");
        check(!contains(script, "captureWarnings"),
              "warning routing moved into the module too");

        const std::string module = AseScriptGenerator::loggerModuleSource();
        check(!module.empty(), "module source is baked into the binary");
        checkContains(module, "class CalangoLog", "module defines CalangoLog");
        checkContains(module, "def metric", "module has metric()");
        checkContains(module, "def progress", "module has progress()");
        checkContains(module, "def event", "module has event()");
        check(std::string(AseScriptGenerator::loggerModuleFileName())
                  == "calango_log.py",
              "module file name matches the import");
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
    std::printf("GPAW 25.7 / 26.7 compatibility:\n");
    {
        ElectronicConfig bands;
        bands.backend = ElectronicBackend::Gpaw;
        bands.pdos = true;
        const std::string script = generateElectronicScript(bands);
        // get_orbital_ldos is gone from the new engine; every projection was
        // swallowed by `except Exception: continue`, so the run "succeeded"
        // with no PDOS at all. DOSCalculator.raw_pdos is identical in both.
        checkContains(script, "from gpaw.dos import DOSCalculator",
                      "PDOS goes through the portable DOSCalculator");
        checkContains(script, "raw_pdos(", "and its raw_pdos entry point");
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
        checkContains(base, "frequencies=frequencies_eV",
                      "and is handed to DielectricFunction");
        checkContains(base, "hilbert=False",
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
        checkContains(base, "\"npoints\": int(len(frequencies))",
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

    // -- Wannier Fermi surface -------------------------------------------------
    std::printf("Wannier Fermi surface:\n");
    {
        FermiSurfaceConfig cfg;
        cfg.mlwfDir = "/jobs/proc_9";
        cfg.gridSamples = 36;
        const std::string script = generateFermiSurfaceScript(cfg);
        checkContains(script, "_n = 36", "honors the requested grid");
        checkContains(script, "get_hamiltonian_kpoint",
                      "interpolates H(R) -> H(k) rather than re-running SCF");
        checkContains(script, "(np.arange(_n) / _n) - 0.5",
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
        tiny.gridSamples = 1;
        checkContains(generateFermiSurfaceScript(tiny), "_n = 4",
                      "a grid too small to triangulate is clamped");
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
        checkContains(script, "NequIPCalculator.from_deployed_model",
                      "Allegro loads through the NequIP deployed-model API");
        checkContains(script, "device=\"cuda\"", "device is honored");
        checkContains(script, "mir-allegro", "names the Allegro package");
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
        CalculatorConfig c;
        c.calculator = CalculatorKind::ChgNet;
        c.chgnetWeights = ChgNetWeights::Latest;
        c.chgnetStress = false;
        const std::string script = AseScriptGenerator::calculatorSnippet(c);
        checkContains(script, "model_name=\"latest\"", "weight set is honored");
        checkContains(script, "stress_weight=0.0", "stress toggle is honored");
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
        // The failure mode this guards: an inherited baseline whose k-grid is
        // not high-symmetry. Falling back would return a spectrum from a
        // different integrator than the one requested.
        checkContains(script, "vertices of the IBZ",
                      "detects the incompatible-grid error");
        checkContains(script, "find_high_symmetry_monkhorst_pack",
                      "names the remedy in the error message");
        check(!contains(script, "integrationmode = \"point integration\""),
              "does not silently fall back to point integration");
    }

    // -- Response k-mesh and IBZ reduction -----------------------------------
    std::printf("Optics response sampling:\n");
    {
        OpticsConfig optics;
        optics.baselineDensityPath = "/jobs/proc_1/single_point.gpw";
        const std::string script = generateOpticsScript(optics);
        check(!contains(script, "kpts=_response_kpts"),
              "no k-mesh line when every axis is left to the baseline");
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
        checkContains(script, "from ase.calculators.dftd4 import DFTD4",
                      "imports the ASE DFTD4 wrapper");
        // The damping parameters are fitted per functional, so D4 must be told
        // which one it corrects — following the calculator's own xc.
        checkContains(script, "atoms.calc = DFTD4(method=\"PBEsol\", calc=atoms.calc)",
                      "wraps the calculator and follows its functional");
    }
    {
        // The wrap must apply to every calculator, not just GPAW.
        CalculatorConfig c = maceConfig();
        c.dispersionD4 = true;
        checkContains(AseScriptGenerator::generate(c, "structure.extxyz"),
                      "calc=atoms.calc)", "wraps a non-DFT calculator too");
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

    std::printf(failures == 0 ? "\nAll script checks passed.\n"
                              : "\n%d check(s) FAILED.\n",
                failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
