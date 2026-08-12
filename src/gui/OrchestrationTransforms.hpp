#pragma once

#include "core/Structure.hpp"

#include <QJsonObject>
#include <QList>
#include <QString>

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
    /// What the recipe's operations MEAN together.
    enum class Mode {
        /// One material carrying every operation at once — a di-vacancy, a
        /// substitution next to an interstitial, a complex.
        Combined,
        /// One material PER operation, each applied on its own to the pristine
        /// incoming structure. A set of singly-defective cells, which is what a
        /// formation-energy or dopant-screening study needs: the whole point is
        /// that the defects do not see each other.
        Separate,
    };

    QList<DefectOperation> operations;
    Mode mode = Mode::Combined;

    bool isEmpty() const { return operations.isEmpty(); }
    /// How many materials this recipe produces: 1 combined, one per operation
    /// separate.
    int variantCount() const
    {
        if (operations.isEmpty())
            return 0;
        return mode == Mode::Separate ? static_cast<int>(operations.size()) : 1;
    }
    /// "remove 12; substitute 3 with B"
    QString describe() const;

    QJsonObject toJson() const;
    static DefectSpec fromJson(const QJsonObject& object);
    static QString modeName(Mode mode);
};

/// Settings of a TDB Generator node: fit a CALPHAD solution model to the
/// formation energies an upstream ensemble produced, and write a `.tdb`.
///
/// The odd one out among the transforms, and deliberately so. It runs on the
/// canvas in process like the others, for the same reason — a polynomial fit
/// and some text formatting are microseconds of work, not a job — but it
/// consumes a completed run's RESULTS rather than a structure, and it produces
/// a database rather than a structure. So it declares an input SLOT
/// (`cluster_expansion.json`, the file the convex-hull viewer already reads)
/// instead of taking the ordinary geometry handoff, and it emits no
/// `transformed.extxyz`.
///
/// STATIC BY CONSTRUCTION, and the node says so on its face and in the file it
/// writes. A cluster-expansion ensemble carries total energies and nothing
/// else, so the excess Gibbs energy fitted here is a pure enthalpy and every
/// excess entropy in the emitted database is exactly zero. Adding the
/// vibrational term needs one phonon calculation per configuration, which is
/// not something a single upstream link can supply; the standalone
/// "From DFT…" dialog is where that is done.
struct TdbGeneratorSpec {
    /// Endpoint names. Empty means "take them from the ensemble file", which
    /// is the usual case — the file records which element the composition axis
    /// counts, and the formulas name the other.
    QString elementA;
    QString elementB;
    QString phaseName = QStringLiteral("FCC_A1");
    /// Highest Redlich-Kister order fitted. Order 0 is the regular solution.
    int order = 2;
    /// The temperature the database declares its parameters valid over. With a
    /// static assessment the coefficients do not depend on it, but the range
    /// is written into the file and a solver reading it will refuse outside.
    double lowTemperatureK = 298.15;
    double highTemperatureK = 3000.0;

    bool isValid() const
    {
        return order >= 0 && order <= 5 && highTemperatureK > lowTemperatureK;
    }
    /// "FCC_A1, Redlich-Kister order 2"
    QString describe() const;

    QJsonObject toJson() const;
    static TdbGeneratorSpec fromJson(const QJsonObject& object);
};

/// One defective material produced by a Defect Generator.
struct DefectVariant {
    /// What was done to it — "substitute 0, 4 with N". Names its tab in the
    /// workspace and its directory in the run folder, so a set of twelve
    /// dopants is readable rather than twelve numbered folders.
    QString label;
    core::Structure structure;
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

/// Every material `spec` describes.
///
/// One entry in Combined mode, one per operation in Separate mode — where each
/// operation is applied to the PRISTINE incoming structure rather than to the
/// output of the previous one, which is the entire difference between "a set of
/// singly-defective cells" and "a cell with N defects in it".
///
/// Empty (with `error` set) if any operation fails; a partial set is never
/// returned, because a screening study missing the one dopant that could not be
/// placed is a study whose conclusion is about the wrong set.
QList<DefectVariant> applyDefectSet(const core::Structure& structure,
                                    const DefectSpec& spec, QString* error);

/// What a TDB Generator node writes into its own job directory.
struct TdbGeneratorOutput {
    QString databaseText;  ///< the `.tdb`
    QString summaryJson;   ///< the fit, the samples and the static hull
    QString headline;      ///< one line for the run report / provenance
};

/// Fit a CALPHAD model to the ensemble in a cluster-expansion results file.
///
/// `ensembleJson` is the raw text of `cluster_expansion.json`. Returns false
/// with `error` set for a file that carries no usable ensemble — a run whose
/// configurations all failed, or one with no intermediate composition, which
/// is a set of two pure elements and determines nothing.
bool runTdbAssessment(const QString& ensembleJson, const TdbGeneratorSpec& spec,
                      TdbGeneratorOutput* output, QString* error);

} // namespace calango::gui
