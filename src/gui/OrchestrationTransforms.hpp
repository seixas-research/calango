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

/// One requested alloy composition of an SQS Generator node.
///
/// The label is what names the pass: its job directory, its workspace tab and
/// its row in the run report. Left empty it is derived from the fractions
/// ("Cu75Au25"), because a batch of six compositions whose folders are
/// numbered is six folders nobody can tell apart.
struct AlloyComposition {
    QString label;
    /// Symbol → fraction on the substitutional sublattice. Fractions are
    /// normalized by the generator and rounded onto whole sites, so they need
    /// not sum to 1.
    QList<QPair<QString, double>> species;

    bool isValid() const { return species.size() >= 2; }
    /// "Cu 0.75 / Au 0.25"
    QString describe() const;
    /// `label` if set, else the derived formula.
    QString name() const;

    QJsonObject toJson() const;
    static AlloyComposition fromJson(const QJsonObject& object);
};

/// Settings of an SQS Generator node: decorate the sublattice of the incoming
/// structure so its cluster correlations match those of the ideal random
/// alloy, once per requested composition.
///
/// The ENTRY POINT of the alloy pipeline, and a Transform in the strict sense
/// — a structure in, structures out — which is why it takes the ordinary
/// geometry handoff rather than an input slot. Put the parent lattice in a
/// Structure Container, link it here, and the compositions fan the downstream
/// pipeline out exactly the way a separate-mode Defect Generator does: one
/// pass per composition, each on the pristine incoming cell.
///
/// It runs in process on core::SqsGenerator, like every other transform. A
/// 20 000-step anneal on a hundred-site cell is a tenth of a second; spawning
/// an interpreter for it would cost more than the work.
struct SqsGeneratorSpec {
    /// Supercell of the incoming structure the sublattice is taken from. The
    /// SQS is only as good as the cell is large — correlations it cannot fit
    /// inside the cell it cannot fit at all.
    int na = 2;
    int nb = 2;
    int nc = 2;
    /// Symbol whose sites are decorated. EMPTY means "the most abundant
    /// element in whatever arrives", which is the right default for the usual
    /// case (a single-element parent lattice) and is resolved at run time
    /// because the node normally has no structure when it is configured.
    QString replaceElement;
    /// One output structure per entry, in this order.
    QList<AlloyComposition> compositions;

    double shell1 = 3.2;
    double shell2 = 4.8;
    /// Multi-body cluster cutoffs, 0 = off. Off by default: the pair-only
    /// objective is what an SQS has always meant, and the three-body term is
    /// worth its cost only when three-body energies are.
    double tripletCutoff = 0.0;
    double quadrupletCutoff = 0.0;
    double tripletWeight = 0.5;
    double quadrupletWeight = 0.25;
    int steps = 20000;
    /// Fixed rather than drawn from the clock, and that is not laziness: a
    /// pipeline whose structures change between two runs of the same saved
    /// workflow is a pipeline whose results cannot be reproduced.
    int seed = 42;

    bool isEmpty() const { return compositions.isEmpty(); }
    /// How many materials this node produces — the batch dimension it
    /// contributes.
    int variantCount() const { return static_cast<int>(compositions.size()); }
    bool isValid() const;
    /// "Cu75Au25, Cu50Au50 · 3x3x3 · pairs to 4.8 Å"
    QString describe() const;

    QJsonObject toJson() const;
    static SqsGeneratorSpec fromJson(const QJsonObject& object);
};

/// Settings of a Cluster Expansion (ECI Fitter) node: fit effective cluster
/// interactions to the total energies an upstream ensemble computed.
///
/// Consumes RESULTS, not a structure, so like the TDB Generator it declares an
/// input slot (`cluster_expansion.json` — the same file the convex-hull viewer
/// and the TDB Generator read, so one ensemble cannot become two descriptions
/// of itself) and emits no `transformed.extxyz`.
struct ClusterExpansionFitSpec {
    /// Which regularization decides the orbits. Mirrors core::EciMethod, in
    /// the same order — declared here rather than included for the reason
    /// given on CvmEntropySpec below: these values are persisted in a saved
    /// workflow, and a persisted identity must not be an alias for a solver's
    /// internal enum.
    enum class Method {
        Ridge, ///< L2. Keeps every orbit and shrinks them all; never sparse.
        Lasso, ///< L1. SELECTS orbits, which is a cluster expansion's real
               ///< difficulty. The default.
        Ard,   ///< Sparse Bayesian regression; no penalty to choose at all.
    };
    Method method = Method::Lasso;

    /// Cluster basis the design matrix is built on. An order is included only
    /// when its cutoff is > 0; the pair cutoff is the one that must be set,
    /// because a fit with no pair term is a fit with no chemistry in it.
    double pairCutoff = 6.0;
    double tripletCutoff = 4.5;
    double quadrupletCutoff = 0.0;

    /// Length of the regularization path the cross-validation chooses from.
    int lambdaCount = 50;
    /// 0 means LEAVE-ONE-OUT, which is what "the CV score" means in the
    /// cluster-expansion literature and is core::EciFitOptions's own default.
    /// A positive k is k-fold, worth having once the ensemble runs to
    /// hundreds. 1 is neither, and is refused.
    int crossValidationFolds = 0;
    /// Take the sparsest model within one standard error of the best CV score
    /// rather than the exact minimum. The minimum is flat and noisy on the
    /// sparse side, and its precise location routinely keeps two or three
    /// spurious long-range clusters at no gain.
    bool oneStandardError = false;
    /// Centre and scale the columns before fitting. Without it the UNITS of an
    /// orbit decide how hard the penalty hits it, because neither L1 nor L2 is
    /// scale-invariant.
    bool standardize = true;

    bool isValid() const
    {
        return pairCutoff > 0.0 && lambdaCount >= 1
            && crossValidationFolds >= 0 && crossValidationFolds != 1;
    }
    /// "lasso, pairs 6 Å, triplets 4.5 Å, leave-one-out"
    QString describe() const;
    static QString methodName(Method method);

    QJsonObject toJson() const;
    static ClusterExpansionFitSpec fromJson(const QJsonObject& object);
};

/// Settings of a CVM Entropy Calculator node: configurational entropy against
/// temperature from the ECIs an upstream fit produced.
///
/// The enums mirror core::CvmLattice / core::CvmApproximation but are declared
/// here rather than included from there. The canvas's node types are persisted
/// (a saved workflow round-trips them) and must therefore be stable in a way a
/// solver's internal enum is not; and this way the panel does not fail to
/// compile whenever the solver's header moves under it.
struct CvmEntropySpec {
    /// The lattice the cluster geometry comes from. Not decoration: the
    /// Kikuchi coefficients depend on how many pairs, triangles and tetrahedra
    /// share a site, and getting it wrong does not fail loudly — it produces a
    /// smooth, plausible, wrong entropy.
    enum class Lattice { Fcc, Bcc, Chain };
    /// Point (= ideal, sites independent), Pair (Bethe-Peierls-Guggenheim) or
    /// Tetrahedron (Kikuchi). A hierarchy, not alternatives: each is the
    /// previous plus more correlation, and the SPREAD between them is the size
    /// of the correction.
    enum class Approximation { Point, Pair, Tetrahedron };

    Lattice lattice = Lattice::Fcc;
    Approximation approximation = Approximation::Tetrahedron;
    double minTemperatureK = 100.0;
    double maxTemperatureK = 2000.0;
    int temperatureSteps = 100;

    bool isValid() const
    {
        return maxTemperatureK > minTemperatureK && minTemperatureK > 0.0
            && temperatureSteps >= 2;
    }
    /// "fcc, tetrahedron, 100–2000 K"
    QString describe() const;
    static QString latticeName(Lattice lattice);
    static QString approximationName(Approximation approximation);

    QJsonObject toJson() const;
    static CvmEntropySpec fromJson(const QJsonObject& object);
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

/// What an SQS Generator node produces for ONE composition.
struct SqsGeneratorOutput {
    core::Structure structure;
    /// Names the pass — its directory, its tab, its report row.
    QString label;
    QString summaryJson; ///< ΔΠ by cluster order, Warren-Cowley α, the cell
    QString headline;    ///< one line for the run report / provenance
};

/// Decorate `parent` at `spec.compositions[index]`.
///
/// `index` is clamped into range, which is what makes a node whose composition
/// list shrank after a run still produce something rather than reading off the
/// end — the same rule the Container and the Defect Generator follow.
bool runSqsGeneration(const core::Structure& parent,
                      const SqsGeneratorSpec& spec, int index,
                      SqsGeneratorOutput* output, QString* error);

/// What a Cluster Expansion (ECI Fitter) node writes.
struct ClusterExpansionFitOutput {
    /// `cluster_expansion_fit.json`: the cluster basis, the fitted ECIs, the
    /// residual and the cross-validation score. This is the file the CVM node
    /// stages, so its name is a contract between the two.
    QString eciJson;
    QString headline;
};

/// Fit effective cluster interactions to the ensemble in `ensembleJson` (the
/// raw text of `cluster_expansion.json`).
///
/// The solver lives in core::ClusterExpansionFit, and this function is
/// deliberately the ONLY place the node touches it — nothing in the canvas,
/// the slot tables, the document format or the provenance knows about the
/// solver.
bool runClusterExpansionFit(const QString& ensembleJson,
                            const ClusterExpansionFitSpec& spec,
                            ClusterExpansionFitOutput* output, QString* error);

/// What a CVM Entropy Calculator node writes.
struct CvmEntropyOutput {
    /// `cvm_entropy.json`: S(T), E(T), F(T) and the Warren-Cowley α(T) that
    /// says WHICH way the alloy departs from random, plus the ideal entropy
    /// every curve is compared against.
    QString entropyJson;
    QString headline;
};

/// Solve the CVM free-energy minimization over a temperature range, from the
/// ECIs in `eciJson` (the raw text of `cluster_expansion_fit.json`).
///
/// Same arrangement as runClusterExpansionFit: the physics belongs to
/// core::ClusterVariation, and this function is its one wire-up point.
bool runCvmEntropy(const QString& eciJson, const CvmEntropySpec& spec,
                   CvmEntropyOutput* output, QString* error);

} // namespace calango::gui
