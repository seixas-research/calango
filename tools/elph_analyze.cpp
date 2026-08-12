// calango-elph-analyze — alpha^2F, lambda and tau from a finished gpaw.elph
// run, without the GUI.
//
// The generated script stops at the raw arrays; this turns them into physics.
// Exists as a separate binary for two reasons that are really one: the runs
// that need it most are the ones too big to bring home. |g|^2 is
// (spins, q, k, modes, bands, bands) complex and reaches tens of gigabytes on
// a production mesh, so the analysis belongs next to the run — on the cluster,
// over ssh, in a batch script — rather than after a download.
//
//     calango-elph-analyze <run-directory>
//
// Writes epc.json into that directory and prints the numbers. Exit status is
// non-zero if the analysis refused, so it composes with `&&` in a job script.

#include "core/ElectronPhononAnalysis.hpp"
#include "core/ElectronPhononIo.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

int main(int argc, char** argv)
{
    if (argc != 2 || std::string(argv[1]) == "--help"
        || std::string(argv[1]) == "-h") {
        std::printf(
            "usage: calango-elph-analyze <run-directory>\n"
            "\n"
            "Reads %s and the .npy arrays it names, integrates the two\n"
            "Fermi-surface delta functions with the linear tetrahedron\n"
            "method, and writes epc.json into the same directory.\n"
            "\n"
            "There is no smearing parameter: that is the point of the\n"
            "tetrahedron method here.\n",
            calango::core::electronPhononManifestName());
        return argc == 2 ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    const std::string directory = argv[1];
    calango::core::ElectronPhononResult result;
    std::string error;
    const bool ok =
        calango::core::postProcessElectronPhonon(directory, result, &error);

    // Warnings print whether or not the analysis succeeded — when it failed
    // they are the only useful output, and when it succeeded they are what
    // qualifies the number.
    for (const std::string& warning : result.warnings)
        std::fprintf(stderr, "warning: %s\n", warning.c_str());

    if (!ok) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return EXIT_FAILURE;
    }

    std::printf("lambda            %.4f\n", result.lambda);
    std::printf("omega_log         %.2f meV\n", result.omegaLogEv * 1000.0);
    std::printf("N(E_F)            %.4f states/eV per cell per spin\n",
                result.dosAtFermi);
    std::printf("temperature       %.0f K\n", result.temperatureK);
    std::printf("tau               %.3f fs\n", result.relaxationTimeFs);
    std::printf("hbar/tau          %.5f eV\n", result.scatteringRateEv);
    // Spelled out because the factor of two between this and hbar/tau is the
    // standing trap on the way to the optics module.
    std::printf("Drude rate        %.5f eV  (= hbar/2tau, what GPAW's "
                "DielectricFunction `rate` takes)\n",
                result.drudeRateEv);
    std::printf("m*/m              %.4f  (band-mass enhancement 1 + lambda)\n",
                result.massEnhancement);
    if (result.excludedModes > 0)
        std::printf("excluded modes    %d (imaginary or zero)\n",
                    result.excludedModes);

    // Transport. lambda_tr is the number that actually governs resistivity;
    // lambda governs the mass. They differ by the backscattering weight and
    // conflating them is a standing error.
    if (result.lambdaTransport > 0.0) {
        std::printf("\n-- transport --\n");
        std::printf("lambda_tr         %.4f  (vs lambda %.4f; the difference "
                    "is the 1 - cos(theta) backscattering weight)\n",
                    result.lambdaTransport, result.lambda);
        std::printf("tau_tr            %.3f fs\n",
                    result.relaxationTimeTransportFs);
        if (result.resistivityMicroOhmCm > 0.0)
            std::printf("rho(%.0f K)        %.4f uOhm*cm\n",
                        result.temperatureK, result.resistivityMicroOhmCm);
        else
            std::printf("rho               not computed (no plasma "
                        "frequency given)\n");
        if (result.velocityDegenerateStates > 0)
            std::printf("  %d states had no velocity direction; lambda_tr is "
                        "correspondingly uncertain\n",
                        result.velocityDegenerateStates);
    }

    // T_c last, because it is the number people will read first and it needs
    // its caveats immediately beside it.
    const auto& sc = result.superconductivity;
    std::printf("\n-- superconductivity (mu* = %.3f) --\n", sc.muStar);
    if (!sc.ok) {
        std::printf("not a phonon-mediated superconductor at this coupling\n");
    } else {
        std::printf("T_c               %.3f K  (Allen-Dynes, f1 = %.3f, "
                    "f2 = %.3f)\n",
                    sc.tcAllenDynesCorrectedK, sc.f1, sc.f2);
        std::printf("  uncorrected     %.3f K\n", sc.tcAllenDynesK);
        if (sc.tcMcMillanK > 0.0)
            std::printf("  McMillan        %.3f K\n", sc.tcMcMillanK);
        std::printf("gap 2*Delta       %.4f meV  (2D/kTc = %.2f)\n",
                    2.0 * sc.gapMeV, sc.gapRatio);

        // The sweep, always. mu* is empirical and T_c depends on it
        // exponentially, so a single value carries precision that does not
        // exist. Quote the range.
        std::printf("\nT_c across mu* (quote a RANGE, not one value):\n");
        for (const auto& point : sc.tcVsMuStar)
            std::printf("  mu* = %.2f -> %s\n", point.first,
                        point.second > 0.0
                            ? (std::to_string(point.second).substr(0, 6)
                               + " K")
                                  .c_str()
                            : "not superconducting");
        if (result.retardationLog > 0.0)
            std::printf("\nretardation ln(W/omega_log) = %.2f "
                        "(occupied bandwidth %.2f eV)\n"
                        "  a bare mu of X gives mu* = X/(1 + %.2f X): "
                        "0.30 -> %.3f, 0.50 -> %.3f\n",
                        result.retardationLog, result.occupiedBandwidthEv,
                        result.retardationLog,
                        calango::core::morelAndersonMuStar(
                            0.30, result.occupiedBandwidthEv,
                            result.omegaLogEv),
                        calango::core::morelAndersonMuStar(
                            0.50, result.occupiedBandwidthEv,
                            result.omegaLogEv));
    }
    for (const std::string& warning : sc.warnings)
        std::fprintf(stderr, "warning: %s\n", warning.c_str());
    std::printf("\nwrote %s/epc.json\n", directory.c_str());
    return EXIT_SUCCESS;
}
