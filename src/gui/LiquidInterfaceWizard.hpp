#pragma once

#include "core/SolvationBuilder.hpp"

#include <QWizard>

#include <memory>
#include <optional>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QRadioButton;
class QSpinBox;
class QTableWidget;

namespace calango::gui {

class LiquidInterfaceWizard;

/// Stage 1: the region. Which lattice direction the fluid layer is opened
/// along, how thick it is, how far the substrate is replicated laterally, and
/// how close the fluid may come to the surface.
///
/// Everything on this page is geometry, and all of it is answerable before a
/// single molecule is chosen — which is why it is a page of its own rather
/// than a group box on the fill page.
class InterfaceRegionPage : public QWizardPage {
    Q_OBJECT

public:
    explicit InterfaceRegionPage(LiquidInterfaceWizard* wizard);

    void initializePage() override;
    bool isComplete() const override;

private Q_SLOTS:
    void refresh();

private:
    LiquidInterfaceWizard* wizard_;
    QComboBox* axisCombo_;
    QDoubleSpinBox* thicknessSpin_;
    QSpinBox* lateralSpins_[2];
    QDoubleSpinBox* clearanceSpin_;
    QCheckBox* anchorCheck_;
    QLabel* summaryLabel_;
    bool valid_ = false;
};

/// Stage 2: what goes in it. A solvent mixture given as mole fractions, an
/// optional list of ionic species or salts given as formula units, and the
/// amount — either a target mass density or an explicit molecule count.
class SolvationPage : public QWizardPage {
    Q_OBJECT

public:
    explicit SolvationPage(LiquidInterfaceWizard* wizard);

    void initializePage() override;
    bool isComplete() const override;
    bool validatePage() override;

private Q_SLOTS:
    void addComponentRow();
    void removeComponentRow();
    void addIonRow();
    void removeIonRow();
    void refresh();

private:
    /// Fill a combo with the library species of the given categories.
    static void populate(QComboBox* combo,
                         std::initializer_list<core::SolvationBuilder::Category>
                             categories);
    /// The mixture and ion lists as the builder wants them.
    std::vector<core::SolvationBuilder::Component> components() const;
    std::vector<core::SolvationBuilder::IonicComponent> ions() const;

    LiquidInterfaceWizard* wizard_;
    QRadioButton* densityRadio_;
    QRadioButton* countRadio_;
    QDoubleSpinBox* densitySpin_;
    QSpinBox* countSpin_;
    QTableWidget* componentTable_;
    QTableWidget* ionTable_;
    QPushButton* removeComponentButton_;
    QPushButton* removeIonButton_;
    QDoubleSpinBox* toleranceSpin_;
    QSpinBox* seedSpin_;
    QLabel* estimateLabel_;
    /// Set once the user edits the density or the mode, after which choosing a
    /// species stops overwriting either. Picking a gas switching the page to
    /// an explicit count is helpful the first time and infuriating the third.
    bool amountTouched_ = false;
};

/// "Build → Liquid / Gas Interface…": open a fluid region on an existing
/// structure and pack it with a liquid, a gas, a mixture, or an ionic
/// solution.
///
///   Stage 1  Geometry & Region — direction, thickness, lateral supercell
///   Stage 2  Solvation & Mixture — species, mole fractions, ions, amount
///
/// The packing itself is core::SolvationBuilder; this class owns only the
/// parameters and the finished cell. Accepting exposes it through result().
class LiquidInterfaceWizard : public QWizard {
    Q_OBJECT

public:
    explicit LiquidInterfaceWizard(
        std::shared_ptr<const core::Structure> substrate,
        QWidget* parent = nullptr);

    const std::optional<core::SolvationBuilder::Result>& result() const
    {
        return result_;
    }

    /// Shared state, owned here and edited by the two pages.
    std::shared_ptr<const core::Structure> substrate;
    core::SolvationBuilder::Params params;

    /// Run the builder with the current parameters. Returns false and fills
    /// `error` when the request is refused; the caller decides whether that is
    /// a live-preview message or a blocking one.
    bool build(QString* error);

private:
    std::optional<core::SolvationBuilder::Result> result_;
};

} // namespace calango::gui
