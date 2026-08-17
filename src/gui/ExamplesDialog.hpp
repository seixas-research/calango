#pragma once

#include "core/Structure.hpp"
#include "python_bridge/BulkBuilder.hpp"

#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>

#include <memory>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QSpinBox;
class QTableWidget;

namespace calango::gui {

class ViewportWidget;

/// "Database Browser" (Build → From Database…):
///   - Bulk tab: generate a crystal locally with ase.build.bulk (by
///     prototype) or ase.spacegroup.crystal (space group + Wyckoff basis).
///     Element fields have a "Periodic Table…" picker beside them.
///   - Materials Project tab: search by chemical system / formula / mp-id
///     through the user's API key (persisted in QSettings), then open one or
///     many hits — either as separate workspace tabs or grouped into a
///     single multi-frame trajectory.
///   - PubChem tab: fetch a 3D molecular conformer by name, SMILES or CID
///     (no key required).
///   - C2DB tab: search the Computational 2D Materials Database (DTU), by
///     either its live web search (no account needed, but only the filters
///     its own search form actually exposes — formula, energy-above-hull
///     and PBE band-gap range) or a local `c2db.db` ase.db file the user
///     has separately obtained (every field, via ASE's query language,
///     including magnetic state / dynamic stability / layer group, which
///     the live route cannot filter on). See python_bridge/C2db.hpp for the
///     access-route rationale.
/// The dialog only *fetches/builds*; the controller (MainWindow) opens the
/// resulting document via the emitted signals.
class ExamplesDialog : public QDialog {
    Q_OBJECT

public:
    explicit ExamplesDialog(QWidget* parent = nullptr);

Q_SIGNALS:
    void structureFetched(std::shared_ptr<core::Structure> structure,
                          const QString& name);
    /// Several structures collected into one trajectory document (the
    /// "Group Selected into Single Trajectory File" action).
    void trajectoryFetched(
        std::vector<std::shared_ptr<core::Structure>> frames,
        const QString& name);

private Q_SLOTS:
    void fetchFromMaterialsProject();
    void fetchFromPubChem();
    void buildBulkCrystal();
    void searchMaterialsProject();
    void openSelectedSeparately();
    void groupSelectedIntoTrajectory();
    void searchC2db();
    void openC2dbSelectedSeparately();
    void groupC2dbSelectedIntoTrajectory();
    void updateC2dbModeVisibility();

private:
    // -- Bulk tab -----------------------------------------------------------
    QWidget* createBulkTab();
    /// Show/hide the parameter rows the selected build mode / prototype uses.
    void updateBulkParameterVisibility();
    /// When the formula field holds a single element, auto-set the crystal
    /// structure + lattice constant(s) from ASE's tabulated ground-state
    /// reference (e.g. Cu → fcc a≈3.61, Fe → bcc, C/Si → diamond).
    void autoFillBulkReference();
    /// Read the prototype-mode widgets into an ase.build.bulk spec.
    pybridge::BulkBuilder::PrototypeSpec currentPrototypeSpec() const;
    /// Rebuild the embedded 3D preview from the current prototype parameters
    /// (no-op / cleared in space-group mode; silently keeps the last valid
    /// preview when the parameters are mid-edit / invalid).
    void refreshBulkPreview();
    /// Wire a "Periodic Table…" button that appends/sets an element symbol
    /// on `target`.
    QPushButton* makePeriodicTableButton(QWidget* parent, QLineEdit* target,
                                         bool append);

    // -- Materials Project tab ---------------------------------------------
    QWidget* createMaterialsProjectTab();
    /// mp-ids of the currently selected result rows, in table order.
    QStringList selectedMaterialIds() const;
    /// One successfully fetched entry, paired with the id it came from so
    /// callers can label tabs/frames without re-deriving the mapping when
    /// some ids fail.
    struct FetchedEntry {
        QString materialId;
        std::shared_ptr<core::Structure> structure;
    };
    /// Fetch each id, reporting progress; returns what was retrieved and
    /// appends per-id failures to `errors`.
    std::vector<FetchedEntry> fetchSelected(const QStringList& ids,
                                            QStringList& errors);
    void setMaterialsProjectBusy(bool busy);

    QWidget* createPubChemTab();

    // -- C2DB tab -------------------------------------------------------------
    QWidget* createC2dbTab();
    /// uids of the currently selected result rows, in table order.
    QStringList selectedC2dbUids() const;
    void setC2dbBusy(bool busy);
    bool c2dbOnlineMode() const;

    QString apiKey() const;

    // Materials Project
    QLineEdit* apiKeyEdit_ = nullptr;
    QLineEdit* materialIdEdit_ = nullptr;
    QPushButton* fetchButton_ = nullptr;
    QLabel* fetchStatus_ = nullptr;
    QLineEdit* searchEdit_ = nullptr;
    QComboBox* searchModeCombo_ = nullptr;
    QSpinBox* searchLimitSpin_ = nullptr;
    QPushButton* searchButton_ = nullptr;
    QLineEdit* filterEdit_ = nullptr;
    QTableWidget* resultsTable_ = nullptr;
    QPushButton* openSeparatelyButton_ = nullptr;
    QPushButton* groupTrajectoryButton_ = nullptr;

    // PubChem
    QComboBox* pubchemFieldCombo_ = nullptr;
    QLineEdit* pubchemQueryEdit_ = nullptr;
    QPushButton* pubchemButton_ = nullptr;
    QLabel* pubchemStatus_ = nullptr;

    // C2DB
    QComboBox* c2dbModeCombo_ = nullptr; ///< 0 = online, 1 = local .db file
    QLineEdit* c2dbLocalPathEdit_ = nullptr;
    QPushButton* c2dbLocalBrowseButton_ = nullptr;
    QLineEdit* c2dbFormulaEdit_ = nullptr;
    QCheckBox* c2dbUseEhullMax_ = nullptr;
    QDoubleSpinBox* c2dbEhullMaxSpin_ = nullptr;
    QCheckBox* c2dbUseGapMin_ = nullptr;
    QDoubleSpinBox* c2dbGapMinSpin_ = nullptr;
    QCheckBox* c2dbUseGapMax_ = nullptr;
    QDoubleSpinBox* c2dbGapMaxSpin_ = nullptr;
    QComboBox* c2dbDynStabCombo_ = nullptr; ///< local-db only: Any/Yes/No
    QLineEdit* c2dbMagStateEdit_ = nullptr; ///< local-db only: exact match
    QLineEdit* c2dbLayerGroupEdit_ = nullptr; ///< local-db only: exact match
    QLabel* c2dbLocalOnlyNote_ = nullptr;
    QSpinBox* c2dbLimitSpin_ = nullptr;
    QPushButton* c2dbSearchButton_ = nullptr;
    QTableWidget* c2dbResultsTable_ = nullptr;
    QPushButton* c2dbOpenSeparatelyButton_ = nullptr;
    QPushButton* c2dbGroupTrajectoryButton_ = nullptr;
    QLabel* c2dbStatus_ = nullptr;

    // Bulk
    QComboBox* bulkModeCombo_ = nullptr;
    QLineEdit* bulkFormulaEdit_ = nullptr;
    QComboBox* bulkStructureCombo_ = nullptr;
    QDoubleSpinBox* bulkASpin_ = nullptr;
    QCheckBox* bulkUseB_ = nullptr;
    QDoubleSpinBox* bulkBLatticeSpin_ = nullptr;
    QCheckBox* bulkUseCovera_ = nullptr;
    QDoubleSpinBox* bulkCoveraSpin_ = nullptr;
    QCheckBox* bulkUseC_ = nullptr;
    QDoubleSpinBox* bulkCSpin_ = nullptr;
    QCheckBox* bulkCubicCheck_ = nullptr;
    QCheckBox* bulkOrthoCheck_ = nullptr;
    QSpinBox* bulkSpaceGroupSpin_ = nullptr;
    /// The space-group page has its own a/c editors (a widget can only live
    /// in one layout, and the prototype page's `a` means something slightly
    /// different — a conventional constant rather than a cell parameter).
    QDoubleSpinBox* bulkSgASpin_ = nullptr;
    QDoubleSpinBox* bulkSgCSpin_ = nullptr;
    QDoubleSpinBox* bulkBSpin_ = nullptr;
    QDoubleSpinBox* bulkAlphaSpin_ = nullptr;
    QDoubleSpinBox* bulkBetaSpin_ = nullptr;
    QDoubleSpinBox* bulkGammaSpin_ = nullptr;
    QCheckBox* bulkPrimitiveCheck_ = nullptr;
    QTableWidget* bulkSitesTable_ = nullptr;
    QWidget* bulkPrototypePage_ = nullptr;
    QWidget* bulkSpaceGroupPage_ = nullptr;
    QPushButton* bulkBuildButton_ = nullptr;
    QLabel* bulkStatus_ = nullptr;
    ViewportWidget* bulkPreview_ = nullptr; ///< live 3D preview of the target cell
    /// Guards autoFillBulkReference()'s programmatic combo/spin writes from
    /// being treated as user edits.
    bool loadingBulkReference_ = false;
};

} // namespace calango::gui
