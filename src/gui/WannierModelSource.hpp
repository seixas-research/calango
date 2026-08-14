#pragma once

// Required on Linux: libstdc++ (GCC 13+) no longer pulls <cstdint> in
// transitively. Do not remove even if clangd reports it unused on macOS.
#include <cstdint>

#include "core/WannierHamiltonian.hpp"

#include <QString>
#include <QWidget>

class QLabel;
class QDoubleSpinBox;

namespace calango::gui {

/// The "where does the Wannier Hamiltonian come from" row, shared by the
/// Boltzmann Transport and Berry Phase panels.
///
/// Both modules need exactly the same thing — an H(R) plus a cell — and both
/// offer the same two ways to get one: read a Hamiltonian the user already has
/// through Calango's own parser, or build a demonstration model in memory.
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

    /// True once a model has been loaded or built.
    bool hasModel() const { return loaded_; }
    const core::WannierHamiltonian& model() const { return model_; }

Q_SIGNALS:
    /// A new Hamiltonian is available.
    void modelChanged();

private Q_SLOTS:
    void browse();
    void loadCubicDemo();
    void loadChernDemo();

private:
    void adopt(core::WannierHamiltonian model, const QString& description);

    core::WannierHamiltonian model_;
    bool loaded_ = false;
    QLabel* summary_ = nullptr;
    QDoubleSpinBox* latticeConstant_ = nullptr;
};

} // namespace calango::gui
