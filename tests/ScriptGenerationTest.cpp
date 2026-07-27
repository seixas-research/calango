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
#include "core/ElectronicScriptGenerator.hpp"
#include "core/ElfScriptGenerator.hpp"
#include "core/GwScriptGenerator.hpp"
#include "core/OpticsScriptGenerator.hpp"
#include "core/PhononScriptGenerator.hpp"

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
    {
        ElfConfig elf;
        elf.calculator = gpawConfig();
        const std::string script = generateElfScript(elf);
        // gpaw.elf.ELF is gone from BOTH 25.7 and 26.7 — ELF was dead on every
        // currently shipping GPAW until this.
        checkContains(script, "from gpaw.elf import elf_from_dft_calculation",
                      "ELF uses the free function that replaced the class");
        checkContains(script, "getattr(elf_grid, 'data', elf_grid)",
                      "and unwraps the UGArray it returns");
    }
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
        check(!contains(script, "kpts=("),
              "no k-mesh line when the baseline grid is inherited");
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
        checkContains(script, "kpts=(12, 12, 8)",
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
        // A partially specified mesh is not a mesh — 12x12x0 would be an
        // invalid grid, so it must fall back to inheriting rather than emit it.
        OpticsConfig optics;
        optics.baselineDensityPath = "/jobs/proc_1/single_point.gpw";
        optics.responseKpts[0] = 12;
        optics.responseKpts[1] = 12;
        check(!contains(generateOpticsScript(optics), "kpts=("),
              "an incomplete mesh is ignored rather than emitted");
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
