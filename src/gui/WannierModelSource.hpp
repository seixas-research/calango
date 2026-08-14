#pragma once

// Required on Linux: libstdc++ (GCC 13+) no longer pulls <cstdint> in
// transitively. Do not remove even if clangd reports it unused on macOS.
#include <cstdint>

#include "core/WannierHamiltonian.hpp"

#include <QList>
#include <QPair>
#include <QString>
#include <QWidget>

class QComboBox;
class QLabel;
class QDoubleSpinBox;

namespace calango::gui {

/// The "where does the Wannier Hamiltonian come from" row, shared by the
/// Boltzmann Transport and Berry Phase panels.
///
/// Both modules need exactly the same thing — an H(R) plus a cell — and both
/// offer the same three ways to get one: take it from a Wannier run this
/// session completed, read a Hamiltonian the user already has through
/// Calango's own parser, or build a demonstration model in memory.
///
/// The first is listed first because it is the one that closes the loop. Until
/// Calango's Wannier run emitted H(R) these panels could only be driven by a
/// wannier90 file or by a toy, which made a natively-implemented solver
/// reachable only through the code it was written to replace.
/// Factored out so the two panels cannot drift apart on the one input they
/// share, and so a third consumer costs a widget rather than a copy.
///
/// Reading a `_hr.dat` is NOT a dependency on the code that wrote it: the file
/// is a plain text table and the parser is Calango's. Nothing here executes
/// wannier90, postw90 or BoltzWann.
class WannierModelSource : public QWidget {
    Q_OBJECT

public:
    explicit WannierModelSource(QWidget* parent = nullptr);

    /// Offer the completed Wannier runs in this session as sources. Each entry
    /// is (display label, absolute run directory); the run's own
    /// `wannier_hr.dat` and cell are read from it. Call before showing the
    /// panel — with no runs the selector says so rather than sitting empty.
    void setWannierRuns(const QList<QPair<QString, QString>>& runs);

    /// True once a model has been loaded or built.
    bool hasModel() const { return loaded_; }
    const core::WannierHamiltonian& model() const { return model_; }

Q_SIGNALS:
    /// A new Hamiltonian is available.
    void modelChanged();

private Q_SLOTS:
    void runSelected(int index);
    void browse();
    void loadCubicDemo();
    void loadChernDemo();

private:
    void adopt(core::WannierHamiltonian model, const QString& description);
    void clearRunSelection();

    core::WannierHamiltonian model_;
    bool loaded_ = false;
    QLabel* summary_ = nullptr;
    QComboBox* runCombo_ = nullptr; ///< completed Wannier runs, index 0 = none
    QDoubleSpinBox* latticeConstant_ = nullptr;
};

} // namespace calango::gui
