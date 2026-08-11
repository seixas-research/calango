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

/// An integer 3x3 transformation matrix P: the supercell's lattice vectors are
/// P · (old cell), row by row.
///
/// The same mathematics as Build → "Supercell (Transformation Matrix)", and
/// deliberately so — a canvas that could only repeat along the axes would be
/// unable to express the cells that matter most in practice. A rotated
/// orthorhombic cell of a hexagonal lattice, a sqrt(3)×sqrt(3) R30 surface
/// reconstruction and a conventional cell built from a primitive one are all
/// non-diagonal, and none of them is reachable with three multipliers.
///
/// |det P| is the number of primitive cells in the supercell, so det P = 0 is
/// not a degenerate case to tolerate but three coplanar vectors: not a cell.
struct SupercellSpec {
    /// Row-major, so p[i] is the i-th new lattice vector in units of the old
    /// ones. Identity by default.
    int p[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};

    /// Determinant, exactly, in long arithmetic. Also the cell-count multiplier.
    long determinant() const;
    /// A transformation that changes nothing. Allowed, but worth naming so the
    /// node can say so rather than looking configured.
    bool isIdentity() const;
    /// True when P is P = diag(na, nb, nc) with every entry >= 1 — the case
    /// that reads as "2 x 2 x 1" and that older documents can express.
    bool isDiagonal() const;
    bool isValid() const { return determinant() != 0; }
    /// "2 x 2 x 1" when diagonal, otherwise "[[1,1,0],[-1,1,0],[0,0,1]] (x2)".
    QString describe() const;

    /// Equivalent diagonal repetitions, valid only when isDiagonal().
    int na() const { return p[0][0]; }
    int nb() const { return p[1][1]; }
    int nc() const { return p[2][2]; }
    /// P = diag(na, nb, nc).
    static SupercellSpec diagonal(int na, int nb, int nc);

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
