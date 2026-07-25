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
