#pragma once

#include "core/KPath.hpp"
#include "core/Structure.hpp"

#include <pybind11/pybind11.h>

#include <memory>
#include <string>
#include <vector>

namespace calango::pybridge {

/// Stateless conversion layer between core::Structure and ase.Atoms.
///
/// All file I/O deliberately goes through ase.io — one code path handles
/// XYZ, extended XYZ, CIF, POSCAR/CONTCAR and every other format ASE knows.
/// All functions throw std::runtime_error (with the Python traceback text)
/// on failure and must be called with PythonEngine alive, on the GUI thread.
class AseBridge {
public:
    /// Load any ASE-readable file into the core model.
    static core::Structure readStructure(const std::string& path);

    /// Write the structure via ase.io.write. `format` is an ASE format name
    /// ("extxyz", "cif", "vasp", ...); empty = infer from extension.
    static void writeStructure(const core::Structure& structure,
                               const std::string& path,
                               const std::string& format = {});

    /// Write a multi-frame trajectory via ase.io.write. `format` is an ASE
    /// format name ("extxyz", "xyz", "traj", "proteindatabank" for PDB
    /// multi-model, ...); empty = infer from extension.
    static void writeTrajectory(
        const std::vector<std::shared_ptr<core::Structure>>& frames,
        const std::string& path, const std::string& format = {});

    /// All frames of a trajectory / multi-frame file (ase.io.read index=":").
    /// `format` is an explicit ASE format hint for ambiguous extensions
    /// (e.g. "lammps-data"); empty lets ASE infer from name/content.
    static std::vector<core::Structure> readTrajectory(const std::string& path,
                                                       const std::string& format = {});

    /// (nx, ny, nz) repetition via ase.Atoms.repeat — requires a defined
    /// cell along the repeated directions.
    static core::Structure makeSupercell(const core::Structure& structure,
                                         int nx, int ny, int nz);

    /// General (possibly non-diagonal) supercell via ase.build.make_supercell:
    /// the new cell is P · (old cell), with P a 3×3 integer transformation
    /// matrix given row-major (p[i] is row i). |det P| must be a nonzero
    /// integer (it equals the number of primitive cells in the supercell).
    /// Requires a fully defined cell. Throws on a singular / zero-determinant P.
    static core::Structure makeSupercellMatrix(const core::Structure& structure,
                                               const int p[3][3]);

    /// Cleave a surface slab via ase.build.surface: (h k l) Miller indices,
    /// number of layers, vacuum padding in Å on each side.
    static core::Structure makeSlab(const core::Structure& structure,
                                    int h, int k, int l, int layers, double vacuum);

    // -- Nanomaterial builders (ase.build wrappers) ------------------------

    /// Periodic graphene sheet (nx × ny cells, lattice constant a in Å).
    static core::Structure buildGraphene(double a, int nx, int ny, double vacuum);

    /// Graphene nanoribbon; `zigzag` selects the edge type, `saturated`
    /// hydrogen-terminates the edges. width/length in ribbon unit cells.
    static core::Structure buildNanoribbon(int width, int length, bool zigzag,
                                           bool saturated, double vacuum);

    /// Carbon nanotube with chiral indices (n, m) and `length` unit cells.
    static core::Structure buildNanotube(int n, int m, int length, double bond,
                                         double vacuum);

    /// TMD monolayer via ase.build.mx2 (e.g. "MoS2", phase "2H" or "1T").
    static core::Structure buildMx2(const std::string& formula, const std::string& phase,
                                    double a, double thickness, int nx, int ny,
                                    double vacuum);

    /// High-symmetry k-points and ASE's suggested band path for the
    /// structure's Bravais lattice (via ase Cell.bandpath()). The path
    /// string concatenates labels, ','-separated per segment ("GXWKG,UX").
    struct BandPathInfo {
        std::vector<core::KPathPoint> specialPoints;
        std::string suggestedPath;
    };
    static BandPathInfo bandPathInfo(const core::Structure& structure);

    /// Simulated powder XRD pattern via the Debye scattering equation
    /// (ase.utils.xrdebye.XrDebye). Periodic structures are repeated
    /// `repeat`× along their periodic directions first — the Debye sum
    /// acts on a finite cluster, so repetition sharpens the Bragg peaks.
    /// `wavelength` in Å (e.g. Cu Kα = 1.54056); the two-theta grid is in
    /// degrees. O(N² · points): keep the repeat modest.
    struct XrdResult {
        std::vector<double> twoTheta;  ///< degrees
        std::vector<double> intensity; ///< arbitrary units
        /// Form-factor model used: "Iwasa" (Waasmaier-Kirfel q-dependent
        /// factors — only when every species is tabulated) or "Z"
        /// (constant f = Z: peak positions exact, relative intensities
        /// approximate at high angles).
        std::string method;
    };
    static XrdResult simulateXrd(const core::Structure& structure,
                                 double wavelength, double twoThetaMin,
                                 double twoThetaMax, int points, int repeat);

    /// Crystallographic symmetry via spglib: space group (international
    /// symbol + number), point group, and crystal system. Empty `error`
    /// on success; on failure (no spglib, no cell, detection error) the
    /// other fields are empty and `error` says why.
    struct SymmetryInfo {
        std::string spaceGroupSymbol;
        int spaceGroupNumber = 0;
        std::string pointGroup;
        std::string crystalSystem;
        int hallNumber = 0;
        /// Per-atom Wyckoff letter and equivalence-class representative index,
        /// index-aligned to the structure's atoms; uniqueSites is the number
        /// of distinct classes (symmetry-inequivalent sites).
        std::vector<std::string> wyckoffLetters;
        std::vector<int> equivalentAtoms;
        int uniqueSites = 0;
        std::string error;
    };
    static SymmetryInfo symmetryInfo(const core::Structure& structure,
                                     double symprec = 1e-3);

    /// Standardized conventional (or primitive) cell via
    /// spglib.standardize_cell. `toPrimitive` reduces to the primitive cell;
    /// `idealize` snaps the lattice to its ideal symmetric form. Throws on an
    /// undefined cell or a spglib failure.
    static core::Structure standardizeCell(const core::Structure& structure,
                                           double symprec, bool toPrimitive,
                                           bool idealize);

    // -- Dump ("training-data writer") node ---------------------------------
    // Deliberately not "structure conversion": these two touch ase.io the
    // same way readStructure/writeStructure do, but the object in play is a
    // calculator's RESULTS (energy, forces, stress), which core::Structure
    // has no field for at all -- so there is nothing to convert THROUGH,
    // only files to read, rename and re-write. Kept here anyway rather than
    // a new translation unit: this class is already the one place every
    // target that touches ASE links against, and a second one would have to
    // be added to the same handful of CMakeLists source lists for no benefit.

    /// One source file for writeDumpTrainingSet(): a completed pass's own
    /// result file (e.g. "single_point.extxyz"), plus a label used only in
    /// DumpWriteResult::excludedReasons.
    struct DumpSourceFile {
        std::string path;
        std::string label;
        /// Overrides the call's own `configType` for THIS frame alone;
        /// empty means "use `configType`". See
        /// gui::DumpSourceFrame::configTypeOverride for why this exists.
        std::string configTypeOverride;
    };

    /// What one writeDumpTrainingSet() call did.
    struct DumpWriteResult {
        int framesWritten = 0;
        int framesExcluded = 0;
        /// One line per excluded frame: "<label>: <why>".
        std::vector<std::string> excludedReasons;
    };

    /// Read every file in `sources`, in order, via ase.io.read, recover
    /// whichever of energy/forces/stress its calculator reported
    /// (get_potential_energy/get_forces/get_stress — each independently
    /// optional; a calculator that does not implement one raises, and the
    /// frame simply does not carry it), and write ONE combined extxyz at
    /// `outputPath` with those properties renamed to the caller's own keys:
    /// `energyKey` into info (scalar), `forcesKey` into arrays (per-atom,
    /// N x 3), `stressKey` into info as ASE's own Voigt-6 (empty
    /// `stressKey` = do not carry stress at all). `configType`, when
    /// non-empty, is stamped into every written frame's info['config_type'].
    ///
    /// A source that fails to read, or that reports no energy, is EXCLUDED
    /// unless `includeIncomplete` is set — never given a placeholder value,
    /// which would silently teach a model a wrong number for a frame that
    /// was never actually computed. `append` opens the ase.io writer in
    /// append mode instead of overwriting.
    ///
    /// Throws std::runtime_error only for a failure that stops the WHOLE
    /// write (e.g. an unwritable output path); a single bad source frame is
    /// reported in the returned DumpWriteResult instead.
    static DumpWriteResult writeDumpTrainingSet(
        const std::vector<DumpSourceFile>& sources,
        const std::string& outputPath, const std::string& energyKey,
        const std::string& forcesKey, const std::string& stressKey,
        const std::string& configType, bool includeIncomplete, bool append);

    // -- Dataset Manager (MLIP training-set assembly) ------------------------
    // Two calls rather than one: prepareDataset() reads every source, renames
    // properties, tags config_type and applies hygiene ONCE, keeping the
    // resulting ase.Atoms list alive on the Python side (`framesHandle`)
    // rather than round-tripping it through C++; the caller then computes a
    // DETERMINISTIC train/validation/test split in C++ (core::DatasetSplit,
    // which this class has no business re-implementing in Python) and hands
    // the three index lists to writeDatasetSplit(), which slices the SAME
    // already-prepared list rather than re-reading every source file a
    // second time.

    /// One source for prepareDataset(): either a single completed
    /// calculation pass's own result file (mirrors DumpSourceFile) or a
    /// directly-loaded, possibly multi-frame .extxyz a user added to a
    /// Dataset Manager node.
    struct DatasetSourceFile {
        std::string path;
        std::string label;
        /// Read every frame in the file (ase.io.read index=":") when true —
        /// a directly loaded .extxyz that may hold many structures; read
        /// just the one frame ase.io.read() gives by default when false — a
        /// completed calculation pass's own result file, exactly like
        /// DumpSourceFile.
        bool wholeFile = false;
        /// "IsolatedAtom" for every frame this source contributes, or empty
        /// to leave config_type as whatever the frame already carries (if
        /// anything). Manual tagging of an externally loaded file uses this
        /// the same way an upstream Single-atom Container's frames do
        /// through Dump's own configTypeOverride mechanism.
        std::string configTypeOverride;
    };

    /// One frame that survived prepareDataset()'s hygiene pass, in the same
    /// order as (and parallel to) the Python list held in
    /// DatasetPrepareResult::framesHandle.
    struct DatasetFrameInfo {
        int sourceIndex = -1; ///< index into the `sources` prepareDataset() was called with
        bool isIsolatedAtom = false;
        bool hasEnergy = false;
        double energyPerAtom = 0.0; ///< only meaningful when hasEnergy
        std::vector<std::string> elements; ///< this frame's unique symbols
    };

    /// What prepareDataset() did.
    struct DatasetPrepareResult {
        std::vector<DatasetFrameInfo> keptFrames;
        int droppedMissing = 0;
        int droppedDuplicate = 0;
        /// One line per frame prepareDataset() flagged (not dropped) as a
        /// numeric outlier.
        std::vector<std::string> outlierWarnings;
        /// Frames kept per source, parallel to `sources`.
        std::vector<int> perSourceKeptCounts;
        /// Opaque: the prepared ase.Atoms list, parallel to `keptFrames`.
        /// Pass straight to writeDatasetSplit() and nowhere else.
        pybind11::object framesHandle;
    };

    /// Read every source, recover energy/forces/(optionally) stress exactly
    /// as writeDumpTrainingSet() does (a live calculator's results take
    /// priority; a frame that already carries the target key names — e.g. a
    /// user's own pre-labelled .extxyz — is read from there instead), rename
    /// to the caller's key names, tag config_type, then apply hygiene:
    /// frames missing energy or forces are dropped when `dropMissing` is
    /// set (kept, un-flagged, otherwise); EXACT-duplicate frames (identical
    /// formula, cell and positions to an earlier-kept one) are dropped when
    /// `dropDuplicates` is set; a kept frame whose |energy/atom| exceeds
    /// `outlierEnergyPerAtomThresholdEv` is FLAGGED (recorded in
    /// `outlierWarnings`) rather than dropped, when `flagOutliers` is set.
    ///
    /// Throws std::runtime_error only for a failure that stops the whole
    /// read; an individual bad source frame is reflected in the returned
    /// counts instead.
    static DatasetPrepareResult prepareDataset(
        const std::vector<DatasetSourceFile>& sources,
        const std::string& energyKey, const std::string& forcesKey,
        const std::string& stressKey, const std::string& defaultConfigType,
        bool dropMissing, bool dropDuplicates, bool flagOutliers,
        double outlierEnergyPerAtomThresholdEv);

    /// What one writeDatasetSplit() call wrote.
    struct DatasetWriteResult {
        int trainWritten = 0;
        int validWritten = 0;
        int testWritten = 0;
    };

    /// Write three subsets of `framesHandle` (flat indices into the SAME
    /// list prepareDataset() returned) to `outputDirectory`/train.extxyz,
    /// valid.extxyz and test.extxyz. An empty index list skips writing that
    /// file entirely rather than writing an empty one.
    static DatasetWriteResult writeDatasetSplit(
        const pybind11::object& framesHandle,
        const std::vector<int>& trainIndices,
        const std::vector<int>& validIndices,
        const std::vector<int>& testIndices,
        const std::string& outputDirectory);

    /// core::Structure -> ase.Atoms (positions, symbols, cell, pbc).
    static pybind11::object toAtoms(const core::Structure& structure);

    /// ase.Atoms -> core::Structure.
    static core::Structure fromAtoms(const pybind11::handle& atoms);
};

} // namespace calango::pybridge
