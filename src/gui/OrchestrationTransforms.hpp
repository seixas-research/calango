#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>

namespace calango::core {
class Structure;
}

namespace calango::gui {

/// Structure-to-structure edits an orchestration node can apply between two
/// simulations, without leaving the canvas.
///
/// These are the two operations a batch study needs and that were previously
/// only reachable by stopping the pipeline, editing the model by hand in the
/// viewport and starting a second pipeline: expand the cell, and put a defect
/// in it. Keeping them as nodes is what lets "relax, expand, make a vacancy,
/// relax again" be one graph.
///
/// They run IN PROCESS rather than as generated Python: the whole operation is
/// a few hundred microseconds of array work, and a node that spawns an
/// interpreter to repeat a cell would spend a thousand times longer starting
/// up than working. It also means a transform node has no calculator, no
/// launch command and nothing to fail in Python.

/// (na, nb, nc) repetition along the three lattice vectors.
struct SupercellSpec {
    int na = 2;
    int nb = 2;
    int nc = 2;

    /// A repetition that changes nothing. Allowed (it is the identity), but
    /// worth naming so the node can say so.
    bool isIdentity() const { return na == 1 && nb == 1 && nc == 1; }
    bool isValid() const { return na >= 1 && nb >= 1 && nc >= 1; }
    /// "2 x 2 x 1"
    QString describe() const;

    QJsonObject toJson() const;
    static SupercellSpec fromJson(const QJsonObject& object);
};

/// One edit in a defect recipe.
struct DefectOperation {
    enum class Kind {
        Substitute, ///< replace the listed atoms with `element`
        Remove,     ///< delete the listed atoms (a vacancy)
        Add,        ///< insert one atom of `element` at `position`
    };

    Kind kind = Kind::Substitute;
    /// Index list in the INCOMING structure's numbering, as typed
    /// ("0, 4, 7-9"). Unused by Add.
    QString indices;
    /// Chemical symbol for Substitute and Add. Unused by Remove.
    QString element = QStringLiteral("N");
    /// Where an added atom goes. Cartesian A when `fractional` is false,
    /// otherwise cell coordinates.
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    bool fractional = false;

    /// "substitute 0, 4 with N"
    QString describe() const;

    QJsonObject toJson() const;
    static DefectOperation fromJson(const QJsonObject& object);
    static QString kindName(Kind kind);
};

/// An ordered recipe. Empty means the node has nothing to do, which is a
/// refusal rather than a pass-through: a Defect Generator that silently
/// forwards the pristine cell would make every downstream formation energy
/// come out as zero with no error anywhere.
struct DefectSpec {
    QList<DefectOperation> operations;

    bool isEmpty() const { return operations.isEmpty(); }
    /// "remove 12; substitute 3 with B"
    QString describe() const;

    QJsonObject toJson() const;
    static DefectSpec fromJson(const QJsonObject& object);
};

/// Repeat `structure` per `spec`.
///
/// Requires a defined cell along every repeated direction — repeating a
/// molecule in vacuum is meaningless, and ASE refuses it, so `error` is set
/// and the input is returned unchanged.
core::Structure applySupercell(const core::Structure& structure,
                               const SupercellSpec& spec, QString* error);

/// Apply `spec` to `structure`.
///
/// Every index is resolved against the INCOMING structure, not against the
/// partially-edited one: removals are collected first and applied in one pass,
/// so "remove 3, substitute 5" means atoms 3 and 5 of the structure the user
/// was looking at, whatever order the operations were typed in. Additions
/// happen last and append to the end.
///
/// An index that does not exist is dropped rather than clamped (the shared
/// parseAtomIndexList rule) but an operation that ends up addressing NOTHING
/// is an error: it means the recipe was written against a different structure,
/// and quietly doing nothing is how a pipeline computes the pristine cell
/// while its author believes it computed a defect.
core::Structure applyDefects(const core::Structure& structure,
                             const DefectSpec& spec, QString* error);

} // namespace calango::gui
