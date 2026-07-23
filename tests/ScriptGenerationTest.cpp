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

    std::printf(failures == 0 ? "\nAll script checks passed.\n"
                              : "\n%d check(s) FAILED.\n",
                failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
