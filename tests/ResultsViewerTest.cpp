// Results-viewer schema test.
//
// The GW and 2D-optics viewers do exactly one thing that can silently break:
// parse a JSON schema that a *Python* script writes. Nothing in the C++ build
// links the two, so a renamed key drifts undetected until a user runs an
// eight-hour G0W0 job and gets an empty window.
//
// The fixtures below are written to match core::generateGwScript and
// core::generateOpticsScript literally — same keys, same nesting, same
// null-for-missing-edge convention. When a generator key changes, this test is
// what fails.
//
// Needs a QApplication (the viewers are QDialogs) but no GL and no display:
// runs under the offscreen platform.

#include "gui/GwResultsWindow.hpp"
#include "gui/OpticsResultsWindow.hpp"
#include "gui/RamanIrViewer.hpp"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QTableWidget>
#include <QTemporaryDir>

#include <cstdio>
#include <cstdlib>

using namespace calango::gui;

namespace {

int failures = 0;

void check(bool condition, const char* what)
{
    std::printf("  %s %s\n", condition ? "ok  " : "FAIL", what);
    if (!condition)
        ++failures;
}

bool writeFile(const QString& path, const QByteArray& contents)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(contents);
    return true;
}

/// Mirrors the `summary` dict in core::generateGwScript. `renormalization` is
/// passed through so the unconverged-result branch can be exercised.
QByteArray gwJson(double dftGap, double gwGap)
{
    return QByteArray(R"({
        "engine": "GPAW",
        "frequency_treatment": "plasmon-pole",
        "screening_cutoff_eV": 100.0,
        "dft_vbm_eV": 5.5, "dft_cbm_eV": )")
        + QByteArray::number(5.5 + dftGap)
        + R"(, "dft_gap_eV": )" + QByteArray::number(dftGap)
        + R"(, "gw_vbm_eV": 5.2, "gw_cbm_eV": )"
        + QByteArray::number(5.2 + gwGap)
        + R"(, "gw_gap_eV": )" + QByteArray::number(gwGap)
        + R"(, "gap_renormalization_eV": )" + QByteArray::number(gwGap - dftGap)
        + R"(,
        "dft_eigenvalues_eV": [[-5.0, 0.0, 5.5, 6.6], [-4.8, 0.2, 5.4, 6.9]],
        "qp_eigenvalues_eV":  [[-5.4, -0.2, 5.2, 7.4], [-5.2, 0.0, 5.1, 7.7]]
    })";
}

/// Mirrors the `results` dict in core::generateOpticsScript, including the
/// `twod_<dir>` blocks the 2D variant appends.
QByteArray opticsJson(bool twoDimensional)
{
    QByteArray json = R"({
        "energy_eV": [0.0, 1.0, 2.0, 3.0],
        "xx": {
            "eps1": [12.0, 13.0, 9.0, 4.0],
            "eps2": [0.0, 0.5, 3.0, 6.0],
            "absorption": [0.0, 1.0e4, 8.0e4, 2.0e5],
            "reflectivity": [0.3, 0.32, 0.36, 0.4],
            "n": [3.4, 3.6, 3.1, 2.2],
            "k": [0.0, 0.07, 0.48, 1.36],
            "loss": [0.0, 0.003, 0.03, 0.11]
        })";
    if (twoDimensional) {
        json += R"(,
        "twod_xx": {
            "alpha_2D_re_A": [8.75, 9.55, 6.36, 2.39],
            "alpha_2D_im_A": [0.0, 0.4, 2.39, 4.77],
            "absorbance": [0.0, 0.0128, 0.0768, 0.1536],
            "sigma_2D_re": [0.0, 0.001, 0.012, 0.036],
            "sigma_2D_im": [0.0, -0.024, -0.032, -0.018]
        })";
    }
    json += "\n    }";
    return json;
}

/// Mirrors the `summary` dict in core::generateRamanIrScript — same keys, same
/// nesting, same "raman.computed" flag the viewer keys its selector off.
QByteArray ramanIrJson(bool ramanComputed)
{
    QByteArray json = R"({
    "formula": "SiO2",
    "atoms": 3,
    "symbols": ["Si", "O", "O"],
    "displacement_A": 0.01,
    "laser_nm": 532.0,
    "temperature_K": 300.0,
    "broadening_cm": 4.0,
    "volume_A3": 113.0,
    "born_charges_source": "/tmp/born/born_charges.json",
    "optics_source": null,
    "raman": {"computed": )";
    json += ramanComputed ? "true, \"eta_eV\": 0.05, \"off_diagonal\": true"
                          : "false";
    json += R"(},
    "modes": [
        {"index": 0, "frequency_cm": 0.0, "frequency_meV": 0.0,
         "ir_intensity_D2_A2_amu": 0.0, "ir_intensity_e2_amu": 0.0,
         "raman_activity_A4_amu": 0.0, "raman_intensity": 0.0,
         "acoustic": true},
        {"index": 1, "frequency_cm": 464.2, "frequency_meV": 57.6,
         "ir_intensity_D2_A2_amu": 0.0, "ir_intensity_e2_amu": 0.0,
         "raman_activity_A4_amu": 21.5, "raman_intensity": 1830.0,
         "acoustic": false},
        {"index": 2, "frequency_cm": 1080.0, "frequency_meV": 133.9,
         "ir_intensity_D2_A2_amu": 4.75, "ir_intensity_e2_amu": 0.206,
         "raman_activity_A4_amu": 0.4, "raman_intensity": 12.0,
         "acoustic": false}
    ],
    "spectrum": {
        "frequency_cm": [0.0, 400.0, 800.0, 1200.0],
        "ir": [0.0, 0.01, 0.2, 3.9],
        "raman": [0.0, 12.0, 900.0, 4.0]
    }
})";
    return json;
}

} // namespace

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        std::printf("could not create a temporary directory\n");
        return EXIT_FAILURE;
    }

    // -- GW viewer ----------------------------------------------------------
    std::printf("GW viewer reads gw.json:\n");
    {
        const QString path = tempDir.filePath(QStringLiteral("gw.json"));
        check(writeFile(path, gwJson(1.1, 2.4)), "fixture written");

        GwResultsWindow window;
        check(window.loadResults(path), "parses a well-formed summary");

        // 2 k-point rows x 4 bands: every state must reach the table, not just
        // the ones that happen to fit the first row's band count.
        auto* table = window.findChild<QTableWidget*>();
        check(table != nullptr, "has a per-state table");
        if (table)
            check(table->rowCount() == 8, "one row per (k-point, band)");
    }
    {
        // A metal: the script writes null for the edges it cannot identify.
        // The viewer must render that as "no gap", not as 0.0 eV — which would
        // read as a closed gap, a physically different claim.
        const QString path = tempDir.filePath(QStringLiteral("gw_metal.json"));
        check(writeFile(path, R"({
            "engine": "Yambo", "frequency_treatment": "real-axis",
            "screening_cutoff_eV": 60.0,
            "dft_vbm_eV": null, "dft_cbm_eV": null, "dft_gap_eV": null,
            "gw_vbm_eV": null, "gw_cbm_eV": null, "gw_gap_eV": null,
            "gap_renormalization_eV": null,
            "dft_eigenvalues_eV": [[-1.0, 0.5]],
            "qp_eigenvalues_eV": [[-1.2, 0.6]]
        })"), "metal fixture written");

        GwResultsWindow window;
        check(window.loadResults(path), "null band edges do not fail the parse");
    }
    {
        GwResultsWindow window;
        check(!window.loadResults(tempDir.filePath(QStringLiteral("absent.json"))),
              "a missing file is reported, not asserted");
        check(!window.loadResults(QStringLiteral(":/")),
              "an unreadable path is reported");
    }

    // -- Optics viewer ------------------------------------------------------
    std::printf("Optics viewer reads optics.json:\n");
    {
        QDir(tempDir.path()).mkpath(QStringLiteral("bulk"));
        const QString dir = tempDir.filePath(QStringLiteral("bulk"));
        check(writeFile(dir + QStringLiteral("/optics.json"), opticsJson(false)),
              "3D fixture written");

        OpticsResultsWindow window(dir);
        check(window.hasData(), "parses a bulk spectrum");
        // The 2D quantities must NOT be offered for a bulk run: alpha_2D and
        // the absorbance are only defined once a vacuum thickness is divided
        // out, and there is none here.
        auto* quantity = window.findChildren<QComboBox*>().value(0);
        check(quantity != nullptr, "has a quantity selector");
        if (quantity)
            check(quantity->count() == 5, "offers only the 3D quantities");
    }
    {
        QDir(tempDir.path()).mkpath(QStringLiteral("sheet"));
        const QString dir = tempDir.filePath(QStringLiteral("sheet"));
        check(writeFile(dir + QStringLiteral("/optics.json"), opticsJson(true)),
              "2D fixture written");

        OpticsResultsWindow window(dir);
        check(window.hasData(), "parses a sheet spectrum");
        auto* quantity = window.findChildren<QComboBox*>().value(0);
        if (quantity) {
            check(quantity->count() == 8,
                  "adds absorbance, polarizability and conductivity");
            // A 2D job is run FOR the sheet observables, so landing on eps is
            // the wrong default.
            check(quantity->currentText().contains(QStringLiteral("A(ω)")),
                  "opens on the absorbance");
        }
    }

    // -- Raman / IR viewer --------------------------------------------------
    std::printf("Raman/IR viewer reads raman_ir.json:\n");
    {
        const QString path = tempDir.filePath(QStringLiteral("raman_ir.json"));
        check(writeFile(path, ramanIrJson(/*ramanComputed=*/true)),
              "fixture written");

        RamanIrViewer viewer;
        check(viewer.loadResults(path), "parses a well-formed summary");
        auto* table = viewer.findChild<QTableWidget*>();
        check(table != nullptr, "has a mode table");
        if (table)
            check(table->rowCount() == 3, "one row per Γ-point mode");
        // Both spectra are selectable once the Raman half actually ran.
        auto* selector = viewer.findChild<QComboBox*>();
        check(selector != nullptr && selector->isEnabled(),
              "the spectrum selector is live for a Raman run");
    }
    {
        // An IR-only run. The Raman curve is legitimately absent, and the
        // selector must not offer a spectrum that was never computed — a flat
        // curve reads as "every mode is Raman-inactive", which is a different
        // claim entirely.
        const QString path =
            tempDir.filePath(QStringLiteral("raman_ir_ironly.json"));
        check(writeFile(path, ramanIrJson(/*ramanComputed=*/false)),
              "IR-only fixture written");

        RamanIrViewer viewer;
        check(viewer.loadResults(path), "parses an IR-only summary");
        auto* selector = viewer.findChild<QComboBox*>();
        check(selector != nullptr && !selector->isEnabled(),
              "the spectrum selector is disabled when Raman was skipped");
    }
    {
        RamanIrViewer viewer;
        check(!viewer.loadResults(
                  tempDir.filePath(QStringLiteral("no_raman.json"))),
              "a missing file is reported, not asserted");
    }

    std::printf(failures == 0 ? "\nAll results-viewer checks passed.\n"
                              : "\n%d check(s) FAILED.\n",
                failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
