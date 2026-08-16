#pragma once

#include "core/Noise.hpp"
#include "core/Structure.hpp"

#include <QJsonObject>
#include <QList>
#include <QPair>
#include <QString>

#include <memory>

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

/// Settings of a Random Noise Setup node: the same core::NoiseOptions the
/// standalone wizard exposes, plus how many perturbed variants to generate
/// and how (see core::buildNoiseEnsemble(), which both paths call).
struct RandomNoiseSpec {
    core::NoiseOptions options;
    /// Perturbed variants, NOT counting frame 0 (the untouched reference,
    /// always included) — variantCount() below is the batch length this
    /// node actually contributes.
    int count = 20;
    /// Each member perturbs the PREVIOUS one (a random walk) instead of
    /// always restarting from the untouched reference.
    bool cumulative = false;
    /// Amplitude ramps from zero at frame 0 (already true, since it is
    /// unperturbed) to the full configured amplitude at the last frame,
    /// instead of every member using the same amplitude.
    bool ramped = false;

    bool isValid() const
    {
        return count > 0 && (options.perturbPositions || options.perturbCell);
    }
    /// How many passes this node contributes to the batch it multiplies —
    /// the perturbed members AND frame 0, since the untouched reference is
    /// itself one of the structures downstream should see, not just an
    /// internal detail of the ramp.
    int variantCount() const { return isValid() ? count + 1 : 0; }
    /// "21 frames (20 perturbed + reference), Gaussian sigma=0.050 A"
    QString describe() const;

    QJsonObject toJson() const;
    static RandomNoiseSpec fromJson(const QJsonObject& object);
};

/// One named structure a Container-family node hands downstream: the same
/// pair OrchestrationNodeItem::BatchItem is, spelled out so this header does
/// not have to include OrchestrationWindow.hpp (which includes THIS header)
/// to name the type.
using NamedStructure = QPair<QString, std::shared_ptr<const core::Structure>>;

/// Settings of a Single-atom Container node: the box size for the isolated-
/// atom reference cells it generates. Everything else about the operation —
/// which elements, how many, PBC — is either derived from the incoming
/// structures or a fixed policy stated on the node's own face.
struct SingleAtomContainerSpec {
    /// Side of the cubic reference cell, in Å. 10 Å is the usual choice for
    /// an isolated-atom reference in a periodic/plane-wave code: large
    /// enough that periodic images do not interact for any element's
    /// interaction range, small enough that a plane-wave basis stays a
    /// reasonable size.
    double boxSizeAngstrom = 10.0;

    bool isValid() const { return boxSizeAngstrom > 0.0; }
    /// "10 Å box, periodic"
    QString describe() const;

    QJsonObject toJson() const;
    static SingleAtomContainerSpec fromJson(const QJsonObject& object);
};

/// One isolated-atom structure per UNIQUE element across `sources`: a single
/// atom of that element, centered in a periodic cubic box of
/// `spec.boxSizeAngstrom`. Elements are returned in order of FIRST
/// appearance across `sources` — deterministic, and readable, since it is
/// the order the source list itself puts them in.
///
/// PBC is ALWAYS on. A large periodic box is the standard approach for the
/// plane-wave/periodic codes this pipeline targets (GPAW, VASP, MACE's own
/// training convention) — an isolated molecule in a non-periodic cell is a
/// DIFFERENT reference energy (no k-point sampling, no plane-wave cutoff
/// truncation error) and is not offered here; see the node's own tooltip.
///
/// Empty (with no error) is a valid answer for an empty `sources`: the
/// caller decides whether "no elements found" is worth refusing over.
QList<NamedStructure> buildSingleAtomBatch(const QList<NamedStructure>& sources,
                                           const SingleAtomContainerSpec& spec);

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

/// Settings of a Dump node: collect every pass of an upstream fan-out's own
/// computed structure and properties, and write them as one extended-XYZ
/// training set (e.g. for MACE).
///
/// The key names are the crux of the node. An extxyz file has no fixed
/// vocabulary for "the reference energy" — every MLIP trainer reads whichever
/// info/array keys it is told to. The MACE preset (applyMaceTrainingPreset())
/// writes REF_energy / REF_forces / REF_stress, matching
/// mace.tools.default_keys.DefaultKeys as shipped in mace 0.3.15 (also
/// mace_run_train's own --energy_key/--forces_key/--stress_key defaults) —
/// verified against the installed package rather than assumed. Deliberately
/// NOT the bare "energy"/"forces"/"stress" ASE itself uses for a live
/// calculator's results: MACE's own data loader refuses stress_key="stress"
/// outright, warning that since ASE 3.23.0b1 the bare key is not safe to
/// round-trip between ASE and MACE, and names this exact REF_ prefix as the
/// fix.
struct DumpSpec {
    /// Where the aggregate file is written. No default: unlike a Supercell's
    /// 2x2x2, there is no path a Dump node could guess that would not be a
    /// claim about the user's filesystem.
    QString outputPath;
    /// Written into every frame's info[energyKey] (a scalar). ASE's own
    /// "energy" by default — deliberately not MACE-safe until the preset is
    /// applied, so a freshly added node's defaults describe plain ASE
    /// results rather than silently claiming a MACE convention nobody chose.
    QString energyKey = QStringLiteral("energy");
    /// Written into every frame's arrays[forcesKey] (N x 3, per atom).
    QString forcesKey = QStringLiteral("forces");
    /// Written into every frame's info[stressKey] as ASE's own Voigt-6.
    /// Empty = do not carry stress at all, which is the honest choice when
    /// nothing upstream computed one.
    QString stressKey;
    /// Free-form; stamped into every frame's info['config_type'] when
    /// non-empty. Left empty, no such key is written.
    QString configType;
    /// A pass whose energy could not be recovered (the calculation failed,
    /// or its result file is missing/unreadable) is dropped rather than
    /// given a placeholder value — a written zero would silently teach a
    /// model a wrong number. Checking this includes such passes anyway,
    /// wherever they still carry SOME usable properties.
    bool includeFailedFrames = false;
    /// Append to an existing file instead of overwriting it.
    bool appendToExistingFile = false;

    bool isValid() const
    {
        return !outputPath.isEmpty() && !energyKey.isEmpty()
            && !forcesKey.isEmpty();
    }
    /// "energy/forces -> training.extxyz" or, with a stress key set,
    /// "REF_energy/REF_forces/REF_stress -> training.extxyz".
    QString describe() const;

    QJsonObject toJson() const;
    static DumpSpec fromJson(const QJsonObject& object);
};

/// Fill `spec`'s key names with mace 0.3.15's own defaults
/// (mace.tools.default_keys.DefaultKeys) — the "MACE training" preset
/// button. Leaves outputPath, configType and the two checkboxes untouched.
void applyMaceTrainingPreset(DumpSpec* spec);

/// One completed pass a Dump node reads from its parent's own results.
struct DumpSourceFrame {
    /// The parent's result file for this pass (e.g.
    /// ".../job_003/single_point.extxyz") — already resolved by the caller,
    /// since which filename carries a calculator's results depends on the
    /// parent's task (Single-Point, Geometry Optimization, ...).
    QString path;
    /// "pass 3 (Al)" — names this frame in the excluded-reasons report and
    /// nowhere else.
    QString label;
    /// Overrides DumpSpec::configType for THIS frame alone; empty means "use
    /// the spec's own value". The one caller today is a Single-atom
    /// Container's downstream: MACE's own data loader recognizes
    /// info['config_type'] == "IsolatedAtom" (verified against the
    /// installed package) to auto-extract E0s and exclude the frame from
    /// ordinary training — a convention the frame's ORIGIN decides, not
    /// whatever free-form tag the rest of the file happens to be using.
    QString configTypeOverride;
};

/// What a Dump node wrote (and could not write) on one run.
struct DumpOutput {
    int framesWritten = 0;
    int framesExcluded = 0;
    /// One line per excluded frame, capped by the caller for display but
    /// kept in full for the summary JSON.
    QStringList excludedReasons;
    /// "97 frames written to training.extxyz (3 excluded)"
    QString headline;
};

/// Read every frame in `sources`, in order, and write ONE combined extxyz at
/// `spec.outputPath` under `spec`'s chosen key names.
///
/// The one place a Dump node touches ase.io — see
/// pybridge::AseBridge::writeDumpTrainingSet(), which does the actual
/// reading and renaming. `false` (with `*error` set) only for a failure that
/// stops the WHOLE write (an unwritable output path); a single bad source
/// frame is reported in the returned DumpOutput instead; it is not a failure
/// of the node.
bool runDump(const QList<DumpSourceFrame>& sources, const DumpSpec& spec,
            DumpOutput* output, QString* error);

/// Which density product a Dump Charge Densities node collects. Spans every
/// named volumetric file Calango's own generators/engines produce — a node
/// configured before its parent has ever run cannot know in advance which
/// engine will feed it, so the dropdown lists all of them rather than
/// filtering by a guess.
enum class DensityProduct {
    GpawAllElectron,
    GpawPseudo,
    GpawSpin,
    GpawHartree,
    GpawElf,
    GpawKineticEnergy,
    VaspChgcar,
    VaspAeccar0,
    VaspAeccar2,
    VaspLocpot,
    VaspElfcar,
};

/// The exact file name a completed pass's job directory carries the chosen
/// product under — core::densityFiles::k* for a GPAW export, VASP's own bare
/// name (VASP writes CHGCAR/AECCAR0/... itself; nothing in this codebase
/// renames them) otherwise.
QString densityProductFileName(DensityProduct product);
/// The extension a collected file gets when NOT compressed to HDF5: ".cube"
/// for the GPAW products (matching the source exactly), a lower-cased form
/// of the VASP name otherwise — VASP's own files carry no extension at all,
/// which cannot be enumerated ("CHGCAR" repeated 100 times in one folder is
/// not a dataset).
QString densityProductExtension(DensityProduct product);
/// Dropdown label — "All-electron density (GPAW)", "CHGCAR (VASP)".
QString densityProductLabel(DensityProduct product);
/// Every DensityProduct, in dropdown order (GPAW's six, then VASP's five).
QList<DensityProduct> densityProducts();
/// Stable persisted identifier ("gpaw_all_electron", "vasp_chgcar") — what
/// DumpDensitiesSpec::toJson() writes, so a saved workflow survives the enum
/// being reordered (the same reason orchestrationTaskSlug() exists).
QString densityProductSlug(DensityProduct product);
/// The inverse; falls back to GpawAllElectron for an unrecognised slug
/// (rather than refusing the whole node) since this is one field of many —
/// see DumpDensitiesSpec::fromJson().
DensityProduct densityProductFromSlug(const QString& slug);

/// Settings of a Dump Charge Densities node: collect the chosen volumetric
/// product from every pass of an upstream fan-out and write one enumerated
/// file per pass into `outputDirectory` — see OrchestrationWindow.cpp's
/// `OrchestrationTask::DumpDensities` case in runTransform() for the actual
/// collection loop (there is no separate runDumpDensities(): unlike Dump,
/// there is no ase.io step to factor out — copying a file, or calling
/// core::VolumetricData::convertToHdf5(), needs nothing OrchestrationWindow
/// does not already have in scope).
struct DumpDensitiesSpec {
    /// Destination folder. No default, for the same reason DumpSpec's
    /// outputPath has none — there is no path this node could guess that
    /// would not be a claim about the user's filesystem. Created if it does
    /// not already exist.
    QString outputDirectory;
    /// "density_" -> density_0000.cube, density_0001.cube, ...
    QString filePrefix = QStringLiteral("density_");
    DensityProduct product = DensityProduct::GpawAllElectron;
    /// Convert each collected file to Calango's compressed HDF5 container
    /// (core::VolumetricData::saveHdf5 — see
    /// docs/sphinx/source/reference/hdf5_density.md) instead of copying it
    /// verbatim. A pass whose density was ALREADY compressed upstream (the
    /// calculator setup page's own HDF5 option) stays compressed either way
    /// — there is no cube/CHGCAR WRITER in this codebase to decompress into,
    /// so "preserve the source format by default" stops there.
    bool compressHdf5 = false;

    bool isValid() const { return !outputDirectory.isEmpty(); }
    /// "All-electron density (GPAW) -> /path/to/folder" or, with HDF5 on,
    /// the same with " (HDF5)" appended.
    QString describe() const;

    QJsonObject toJson() const;
    static DumpDensitiesSpec fromJson(const QJsonObject& object);
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
