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

#include "gui/BandPdosView.hpp"
#include "gui/BandPdosWindow.hpp"
#include "gui/GwResultsWindow.hpp"
#include "gui/OpticsResultsWindow.hpp"
#include "gui/NonlinearOpticsResultsWindow.hpp"
#include "gui/RamanIrViewer.hpp"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QGroupBox>
#include <QLabel>
#include <QListWidget>
#include <QFile>
#include <QTableWidget>
#include <QTemporaryDir>

#include <cmath>
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

    // -- Nonlinear optics viewer -------------------------------------------
    //
    // nlopt.json is the newest of these C++/Python contracts, and the one with
    // the most structure: two dicts keyed by tensor component, an optional
    // linear block, and per-spectrum columns that exist only for a sheet run.
    // The fixture mirrors core::generateNonlinearOpticsScript literally.
    std::printf("Nonlinear optics viewer reads nlopt.json:\n");
    {
        const QString dir = tempDir.filePath(QStringLiteral("nlopt"));
        QDir().mkpath(dir);
        check(writeFile(dir + QStringLiteral("/nlopt.json"), R"({
            "engine": "GPAW",
            "module": "gpaw.nlopt",
            "formula": "MoS2",
            "energy_eV": [0.0, 1.0, 2.0, 3.0],
            "eta_eV": 0.05,
            "gauge": "lg",
            "eshift_eV": 0.7,
            "components": ["yyy", "xxy"],
            "centrosymmetric": false,
            "vacuum_axis": 2,
            "L_z_A": 30.0,
            "shg": {
                "yyy": {
                    "energy_eV": [0.0, 1.0, 2.0, 3.0],
                    "chi2_re_pm_V": [0.0, 12.0, 340.0, 55.0],
                    "chi2_im_pm_V": [0.0, 1.0, 220.0, 90.0],
                    "chi2_abs_pm_V": [0.0, 12.04, 405.0, 105.5],
                    "chi2_sheet_re_nm2_V": [0.0, 0.036, 1.02, 0.165],
                    "chi2_sheet_im_nm2_V": [0.0, 0.003, 0.66, 0.27],
                    "chi2_sheet_abs_nm2_V": [0.0, 0.036, 1.215, 0.316]
                },
                "xxy": {
                    "energy_eV": [0.0, 1.0, 2.0, 3.0],
                    "chi2_re_pm_V": [0.0, 2.0, 40.0, 5.0],
                    "chi2_im_pm_V": [0.0, 0.1, 22.0, 9.0],
                    "chi2_abs_pm_V": [0.0, 2.0, 45.6, 10.3]
                }
            },
            "shift": {
                "yyy": {
                    "energy_eV": [0.0, 1.0, 2.0, 3.0],
                    "sigma_A_V2": [0.0, 1.2e-8, 4.5e-8, 2.0e-8],
                    "sigma_sheet_A_nm_V2": [0.0, 0.036, 0.135, 0.06]
                }
            },
            "linear": {
                "energy_eV": [0.0, 1.0, 2.0, 3.0],
                "chi1_xx_re": [1.5, 1.9, 3.2, 2.1],
                "chi1_xx_im": [0.0, 0.1, 2.4, 1.1],
                "eps_xx_1": [2.5, 2.9, 4.2, 3.1],
                "eps_xx_2": [0.0, 0.1, 2.4, 1.1],
                "eps_yy_1": [2.5, 2.9, 4.2, 3.1],
                "eps_yy_2": [0.0, 0.1, 2.4, 1.1],
                "eps_zz_1": [1.1, 1.1, 1.2, 1.2],
                "eps_zz_2": [0.0, 0.0, 0.1, 0.1]
            }
        })"),
              "fixture written");

        NonlinearOpticsResultsWindow window;
        check(window.loadResults(dir), "parses a well-formed nlopt.json");
        auto* selector = window.findChild<QComboBox*>();
        check(selector != nullptr, "has a spectrum selector");
        if (selector) {
            // Two SHG components + one shift component + the linear block.
            // A selector that offered fewer would be hiding a spectrum the
            // run paid a full band sum for.
            check(selector->count() == 4,
                  "one entry per spectrum the run actually produced");
            // Each entry carries the JSON path it draws from, so the viewer
            // never has to re-derive one from a translated label. Compared as
            // a set: QJsonObject iterates its keys in sorted order, which is
            // the viewer's business and not a contract worth pinning.
            QStringList paths;
            for (int i = 0; i < selector->count(); ++i)
                paths << selector->itemData(i).toString();
            paths.sort();
            check(paths
                      == QStringList({QStringLiteral("linear"),
                                      QStringLiteral("shg/xxy"),
                                      QStringLiteral("shg/yyy"),
                                      QStringLiteral("shift/yyy")}),
                  "each entry carries the JSON path it draws from");
        }
        // The scissors shift has to be visible without opening the file: two
        // runs of the same material that differ only by one are otherwise
        // indistinguishable, and that is a common way to lose an afternoon.
        bool mentionsScissors = false;
        for (auto* label : window.findChildren<QLabel*>())
            if (label->text().contains(QLatin1String("scissors")))
                mentionsScissors = true;
        check(mentionsScissors, "the applied scissors shift is stated");
    }
    {
        // A centrosymmetric run. The numbers are real output from a real job
        // and they are meaningless — every component of an odd-rank tensor
        // vanishes by symmetry — so the viewer has to say so rather than
        // draw them as a spectrum.
        const QString dir = tempDir.filePath(QStringLiteral("nlopt_centro"));
        QDir().mkpath(dir);
        check(writeFile(dir + QStringLiteral("/nlopt.json"), R"({
            "formula": "MgO",
            "energy_eV": [0.0, 1.0],
            "eta_eV": 0.05,
            "gauge": "lg",
            "eshift_eV": 0.0,
            "centrosymmetric": true,
            "vacuum_axis": -1,
            "shg": {"yyy": {"energy_eV": [0.0, 1.0],
                            "chi2_re_pm_V": [0.0, 1e-9],
                            "chi2_im_pm_V": [0.0, 0.0],
                            "chi2_abs_pm_V": [0.0, 1e-9]}},
            "shift": {}
        })"),
              "centrosymmetric fixture written");

        NonlinearOpticsResultsWindow window;
        check(window.loadResults(dir), "parses it");
        bool warns = false;
        for (auto* label : window.findChildren<QLabel*>())
            if (label->text().contains(QLatin1String("inversion centre")))
                warns = true;
        check(warns,
              "and warns that χ⁽²⁾ vanishes identically for this cell");
    }
    {
        NonlinearOpticsResultsWindow window;
        check(!window.loadResults(tempDir.filePath(QStringLiteral("nowhere"))),
              "a missing nlopt.json is reported, not asserted");
    }

    // -- Electronic structure viewer: the two optional sidecars ------------
    //
    // bands.json has been read for a long time; band_symmetry.json and
    // fatbands.json are new, and they are the same kind of contract — keys
    // that core::generateBandSymmetryBlock and the fatband block write in
    // Python, parsed in C++, with nothing in the build linking the two. The
    // fixtures below mirror those generators literally.
    std::printf("Electronic structure viewer reads its sidecars:\n");
    {
        const QString dir = tempDir.filePath(QStringLiteral("bands"));
        QDir().mkpath(dir);
        // Two k-points, two bands, one spin — the smallest thing that still
        // exercises the [spin][kpoint][band] nesting all three files share.
        check(writeFile(dir + QStringLiteral("/bands.json"), R"({
            "x": [0.0, 1.0],
            "special_x": [0.0, 1.0],
            "special_labels": ["G", "K"],
            "efermi": 0.5,
            "energies": [[[-1.0, 2.0], [-0.5, 1.5]]]
        })"), "bands.json fixture written");
        check(writeFile(dir + QStringLiteral("/band_symmetry.json"), R"({
            "symprec": 0.0001,
            "degeneracy_tol_eV": 0.02,
            "efermi": 0.5,
            "space_group": "P6/mmm",
            "space_group_number": 191,
            "nonsymmorphic_residual": 0.0,
            "points": [
              {"label": "G", "kind": "point", "x": 0.0, "kpoint_index": 0,
               "kpoint": [0.0, 0.0, 0.0], "order": 24,
               "classes": ["E", "i"], "projective": false,
               "max_residual": 0.0,
               "character_table": [{"label": "A1g", "dim": 1, "chi": [1, 1]}],
               "spins": [{"spin": 0, "multiplets": [
                   {"bands": [0], "degeneracy": 1, "energy_eV": -1.0,
                    "irreps": ["A1g"], "resolved": true, "label": "A1g",
                    "multiplicities": [1.0], "characters": [1.0],
                    "residual": 0.0}]}]},
              {"label": "G-K", "kind": "line", "x": 0.5, "kpoint_index": 0,
               "kpoint": [0.1, 0.1, 0.0], "order": 4,
               "classes": ["E"], "projective": false, "max_residual": 0.0,
               "character_table": [{"label": "A1", "dim": 1, "chi": [1]}],
               "spins": [{"spin": 0, "multiplets": [
                   {"bands": [0], "degeneracy": 1, "energy_eV": -0.75,
                    "irreps": ["A1"], "resolved": true, "label": "A1",
                    "multiplicities": [1.0], "characters": [1.0],
                    "residual": 0.0}]}]}
            ]
        })"), "band_symmetry.json fixture written");
        check(writeFile(dir + QStringLiteral("/fatbands.json"), R"({
            "efermi": 0.5,
            "max_weight": 0.8,
            "projections": [
              {"label": "C p_z", "atoms": [0, 1], "l": 1, "m": 1,
               "weights": [[[0.0, 0.8], [0.1, 0.7]]]},
              {"label": "C s", "atoms": [0, 1], "l": 0, "m": -1,
               "weights": [[[0.6, 0.0], [0.5, 0.0]]]}
            ]
        })"), "fatbands.json fixture written");

        BandPdosWindow window(dir);
        check(window.hasData(), "parses the band structure");

        const auto& fatbands = window.findChild<BandPdosView*>()->fatbandData();
        check(fatbands.projections.size() == 2,
              "both fatband channels reach the view");
        check(std::abs(fatbands.maxWeight - 0.8) < 1e-9,
              "with the shared normalization the generator computed");
        if (fatbands.projections.size() == 2) {
            // [spin][kpoint][band]: the one index order that is easy to get
            // wrong and impossible to see afterwards, since a transposed
            // weight array still plots.
            const auto& pz = fatbands.projections[0].second;
            check(fatbands.projections[0].first == QStringLiteral("C p_z"),
                  "channels keep the order and labels they were written in");
            check(pz.size() == 1 && pz[0].size() == 2 && pz[0][0].size() == 2,
                  "weights are [spin][kpoint][band], like the energies");
            check(pz.size() == 1 && pz[0].size() == 2
                      && std::abs(pz[0][0][1] - 0.8) < 1e-9,
                  "and the p_z weight lands on the second band, not the first");
        }

        const auto& symmetry =
            window.findChild<BandPdosView*>()->symmetryData();
        check(symmetry.spaceGroup == QStringLiteral("P6/mmm"),
              "the space group reaches the view");
        check(symmetry.labels.size() == 2,
              "both the point and the line multiplet are read");
        if (symmetry.labels.size() == 2) {
            check(symmetry.labels[0].text == QStringLiteral("A1g")
                      && !symmetry.labels[0].onLine,
                  "the high-symmetry point label is marked as a point");
            // Line labels are drawn only on request, so the distinction has to
            // survive the parse or the plot fills with them.
            check(symmetry.labels[1].onLine,
                  "and the segment-midpoint label as a line");
        }

        // Both panels are opt-in: they appear only because the files were
        // there. A run without them must not show empty controls.
        QListWidget* channels = nullptr;
        for (QGroupBox* group : window.findChildren<QGroupBox*>())
            if (group->title().contains(QStringLiteral("Orbital")))
                channels = group->findChild<QListWidget*>();
        check(channels != nullptr && channels->count() == 2,
              "the channel list is populated from the file");

        // -- The CSV export ------------------------------------------------
        //
        // Tidy layout: one row per (k, spin, band) with the state's energy and
        // one weight column per channel. Two k-points x one spin x two bands
        // is four rows plus the header.
        const QString csv = window.fatbandTable();
        const QStringList rows = csv.split(QLatin1Char('\n'),
                                           Qt::SkipEmptyParts);
        check(rows.size() == 5, "the fatband export is header + one row per state");
        check(!rows.isEmpty()
                  && rows.front()
                         == QStringLiteral("k_distance,spin,band,energy_eV,"
                                           "C_p_z,C_s"),
              "with the energy beside the weights and a column per channel");
        if (rows.size() == 5) {
            // Row order is k-major, then band — k = 0, band 2 is the third
            // row, and it is the state carrying the 0.8 p_z weight the
            // fixture put there.
            check(rows[2] == QStringLiteral("0,1,2,2,0.8,0"),
                  "and each row pairs the weight with the energy of its state");
        }
        // Spaces would split a "C p_z" column header into two fields.
        check(!rows.isEmpty() && !rows.front().contains(QStringLiteral("C p")),
              "channel labels are underscored so the header stays parseable");
        // Space-separated variant for gnuplot, same table.
        check(window.fatbandTable(QLatin1Char(' '))
                  .startsWith(QStringLiteral("k_distance spin band energy_eV")),
              "the .dat variant swaps the separator and nothing else");
    }
    // -- Fatband colormaps -------------------------------------------------
    //
    // The transparent low end is not cosmetic: it is the whole reason several
    // orbital channels can be drawn on one set of axes. An opaque zero-weight
    // colour paints over every channel already drawn AND over the dispersion
    // itself, so a six-orbital plot would show only whichever channel happened
    // to be painted last. That property is asserted here rather than left to
    // the eye.
    std::printf("Fatband colormaps superimpose:\n");
    {
        const char* expected[] = {"Greens", "Blues",  "Reds",
                                  "Oranges", "Greys", "Purples"};
        bool namesMatch = true;
        for (int i = 0; i < 6; ++i)
            namesMatch = namesMatch
                && BandPdosView::fatbandColormapName(i)
                    == QString::fromLatin1(expected[i]);
        check(namesMatch, "six sequential colormaps in the documented order");
        check(BandPdosView::fatbandColormapName(6)
                  == BandPdosView::fatbandColormapName(0),
              "and a seventh channel wraps rather than reading out of bounds");

        bool zeroTransparent = true;
        bool fullOpaque = true;
        bool monotone = true;
        for (int map = 0; map < 6; ++map) {
            for (const bool dark : {false, true}) {
                zeroTransparent = zeroTransparent
                    && BandPdosView::fatbandColorAt(map, 0.0, dark).alpha() == 0;
                fullOpaque = fullOpaque
                    && BandPdosView::fatbandColorAt(map, 1.0, dark).alpha() == 255;
                int previous = -1;
                for (int step = 0; step <= 20; ++step) {
                    const int alpha =
                        BandPdosView::fatbandColorAt(map, step / 20.0, dark)
                            .alpha();
                    monotone = monotone && alpha >= previous;
                    previous = alpha;
                }
            }
        }
        check(zeroTransparent,
              "zero weight is FULLY TRANSPARENT, so channels do not occlude");
        check(fullOpaque, "maximum weight is fully opaque");
        check(monotone, "and opacity never decreases with weight");

        // A sequential map is a luminance ramp away from the page. Drawn on a
        // near-black plot background, the matplotlib originals put maximum
        // weight in near-black — invisible. The dark rendering mirrors
        // lightness so the ramp still runs away from the background.
        const QColor lightMax = BandPdosView::fatbandColorAt(1, 1.0, false);
        const QColor darkMax = BandPdosView::fatbandColorAt(1, 1.0, true);
        check(lightMax.lightness() < 96,
              "Blues runs to a dark navy on a light background");
        check(darkMax.lightness() > 160,
              "and to a light blue on a dark one, so it stays visible");
        check(std::abs(lightMax.hslHue() - darkMax.hslHue()) < 20,
              "with the hue preserved — Blues is blue either way");

        // Distinct channels must be distinguishable at full weight, which is
        // where a reader identifies them.
        bool distinct = true;
        for (int a = 0; a < 6; ++a)
            for (int b = a + 1; b < 6; ++b)
                distinct = distinct
                    && BandPdosView::fatbandColorAt(a, 1.0, true)
                        != BandPdosView::fatbandColorAt(b, 1.0, true);
        check(distinct, "and every pair of channels differs at full weight");

        // The list swatch is opaque: it is drawn on the widget palette, not
        // composited over the plot, and a transparent legend entry is blank.
        bool swatchesOpaque = true;
        for (int map = 0; map < 6; ++map)
            swatchesOpaque =
                swatchesOpaque && BandPdosView::fatbandColor(map).alpha() == 255;
        check(swatchesOpaque, "while the channel-list swatches stay opaque");
    }

    {
        // The same window with neither sidecar: the dispersion still loads and
        // the two panels stay hidden.
        const QString dir = tempDir.filePath(QStringLiteral("bands_plain"));
        QDir().mkpath(dir);
        check(writeFile(dir + QStringLiteral("/bands.json"), R"({
            "x": [0.0, 1.0], "special_x": [0.0], "special_labels": ["G"],
            "efermi": 0.0, "energies": [[[-1.0], [-0.5]]]
        })"), "plain bands.json fixture written");
        BandPdosWindow window(dir);
        check(window.hasData(), "a run without the sidecars still loads");
        check(!window.findChild<BandPdosView*>()->fatbandData().valid(),
              "and carries no fatband data");
        check(!window.findChild<BandPdosView*>()->symmetryData().valid(),
              "nor any symmetry labels");
        bool anyVisible = false;
        for (QGroupBox* group : window.findChildren<QGroupBox*>())
            if ((group->title().contains(QStringLiteral("Orbital"))
                 || group->title().contains(QStringLiteral("symmetry")))
                && !group->isHidden())
                anyVisible = true;
        check(!anyVisible, "with both optional panels hidden rather than empty");
    }

    std::printf(failures == 0 ? "\nAll results-viewer checks passed.\n"
                              : "\n%d check(s) FAILED.\n",
                failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
