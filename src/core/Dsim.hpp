#pragma once

#include <string>
#include <vector>

namespace calango::core {

/// Dilute Solution Interpolation (DSI) model for the mixing enthalpy of
/// substitutional A-B alloys, built up from the energetics of dilute
/// binary solutions.
///
/// Working theory: L. Seixas, R. M. Tromer, R. O. Figueiredo, J. M. Almeida,
/// "Enthalpies of mixing from dilute solutions to high-entropy alloys"
/// (2026), ~/dsim.pdf. Equation numbers below are that paper's own; this
/// header/`.cpp` cite them directly so the implementation can be audited
/// against the paper equation by equation, the same convention as
/// `core::Egqca`/`docs/sphinx/source/simulations/egqca.md`.
///
/// ## The model
///
/// A regular-solution binary's mixing enthalpy is a single interaction
/// parameter times x(1-x) (Eq. 5). The DSI model instead lets that
/// parameter itself depend on composition, built from TWO independent
/// dilute-limit measurements rather than one mid-composition fit — the
/// "subregular" solution model (Eq. 6-7):
///
///     Omega_ij^sigma = (M_j[i]*X_i + M_i[j]*X_j) / (X_i + X_j + eps)   (6)
///     DeltaH_mix^sigma(x) = M_2[1]*x*(1-x)^2 + M_1[2]*x^2*(1-x)        (7)
///
/// where M_i[j] ("differential mixing enthalpy of solute i in host j") is
/// the SLOPE of DeltaH_mix at the dilute limit where i is the minority
/// species (Eq. 8):
///
///     M_2[1] =  dDeltaH_mix/dx |_{x=0},   M_1[2] = -dDeltaH_mix/dx |_{x=1}
///
/// and each slope is estimated from ONE ab initio calculation: a single
/// substitutional impurity of i in a supercell of pure j, evaluated against
/// the mixing-enthalpy definition (Eq. 9)
///
///     Deltah_mix(x) = E(A_{1-x}B_x) - [(1-x)*E(A) + x*E(B)]             (9)
///
/// at the one composition a single substitution in that supercell actually
/// realizes, x = 1/N_atoms (Eq. 10):
///
///     M_2[1] = Deltah_mix(x = 1/N_atoms)                               (10)
///
/// and symmetrically M_1[2] = Deltah_mix(x = 1 - 1/N_atoms) for a single A
/// impurity diluted in a supercell of pure B ("following a similar
/// procedure", the paper's text between Eq. 8 and Eq. 9). So the WHOLE
/// binary curve is interpolated from exactly four ab initio numbers: the
/// two pristine-supercell energies (the x=0 and x=1 reference points a
/// mixing enthalpy is measured relative to) and the two single-impurity
/// supercell energies (the two dilute-limit slopes) — no calculation at any
/// intermediate composition is needed, which is the paper's whole point
/// ("dilute solution interpolation").
///
/// ## Unit convention (verified against the paper's own worked example)
///
/// Eq. 9's E(Z) is exactly what it says: the TOTAL energy of an N_atoms
/// supercell — no per-atom division anywhere. An earlier version of this
/// file argued (from wanting an intensive-looking DeltaH_mix(x)) that E(Z)
/// must be per-atom instead; that reasoning was wrong, and running it
/// produced curves ~N_atoms times too small. The x0-weighted subtraction in
/// M[i][j] below is ITSELF what makes M an (effectively) intensive, size-
/// converged quantity — physically, it is exactly the standard single-
/// substitutional-impurity formation energy, E_imp - (N-1)*e_j - e_i for
/// solute i in host j — so no further division by N belongs anywhere in
/// the pipeline. Confirmed two ways: (1) algebraically, M[i][j] as defined
/// below reduces exactly to that formation-energy expression when E[i][i]/
/// E[j][j] are pure-supercell TOTALS of the same N; (2) empirically,
/// against `oncapintada`'s own worked Au-Pt example
/// (examples/testing_subregular_model.ipynb, the paper's own repository):
/// its `energy_matrix` there is populated with raw
/// `atoms.get_potential_energy()` supercell totals (tens of eV, e.g.
/// -85.98 for a 27-atom Au cell) fed directly into `BinaryAlloy`, with NO
/// division by atom count anywhere in the notebook, and the resulting
/// `enthalpy_of_mixing(x)` (default unit "kJ/mol") lands on the same few-
/// kJ/mol scale as the literature Au-Pt data the notebook plots alongside
/// it. `BaseAlloy._convert_energy`'s "energies are computed in eV/atom"
/// docstring is therefore about the OUTPUT (eV per mole of alloy atoms —
/// the ordinary metallurgical sense of "kJ/mol" — which this per-defect M
/// already delivers once x0 = 1/N_atoms is folded in), not a per-atom
/// requirement on the INPUT energy_matrix.
///
/// ## N-component generalization
///
/// Eq. 4 + Eq. 6 generalize directly to N components: sum the pairwise
/// subregular contribution over every unordered pair,
///
///     DeltaH_mix(X) = sum_{i<j} Omega_ij^sigma(X) * X_i * X_j            (4)
///
/// which needs N(N-1) dilute-impurity calculations (one M_i[j] per ORDERED
/// pair) plus N pristine references — N^2 ab initio points total, the
/// paper's own headline scaling result (a quinary alloy needs 5^2 = 25, not
/// the SQS method's d^(N-1)). `energyMatrix()`/`mMatrix()` below are written
/// generally (N x N) for that reason, even though the Calango workflow this
/// ships with (Orchestration-free: one wizard, one generated script, four
/// structures) only ever builds the N=2 binary case — see
/// `docs/simulations/dsim.md`.
///
/// Validity, per the paper: single-phase substitutional solid solutions of
/// metals and covalently-bonded semiconductors (demonstrated on six fcc
/// noble-metal binaries plus the covalent Si-Ge system); the model assumes
/// the SAME crystal structure for both end members (it interpolates a
/// mixing enthalpy between two references of one lattice, not a
/// structural transformation), and needs a supercell large enough that one
/// impurity does not interact with its own periodic images (the paper uses
/// a 3x3x3 fcc primitive-cell supercell, 27 atoms, x0 = 1/27).

/// N x N TOTAL-energy matrix (eV) — every entry the raw energy of an
/// N_atoms supercell (`atoms.get_potential_energy()`-style, NOT divided by
/// atom count). Diagonal entry `[i][i]` is the pure-element supercell's
/// total energy; off-diagonal `[i][j]` (i != j) is the total energy of the
/// supercell where `i` is a single dilute substitutional impurity in a
/// host of pure `j`. Every entry must come from a supercell of the SAME
/// atom count (`dilution` below is one number shared by the whole matrix,
/// matching the paper's fixed-supercell-size convention) for the mixing-
/// enthalpy formula to be well-defined.
using EnergyMatrix = std::vector<std::vector<double>>;

/// Differential mixing enthalpy matrix, `M[i][j]` = M_i[j] in the paper's
/// notation (solute i, host j), eV. Diagonal is always exactly 0. Despite
/// the eV (not eV/atom) unit, this is already the intensive, x0-normalized
/// quantity Eq. 7 needs — see the unit-convention note above.
using MMatrix = std::vector<std::vector<double>>;

/// M[i][j] = E[i][j] - (dilution * E[i][i] + (1 - dilution) * E[j][j])
/// (Eq. 9-10 — see the unit-convention note above: `energyMatrix` holds
/// TOTAL supercell energies, not per-atom ones). `energyMatrix` must be
/// square; `dilution` (x0 = 1/N_atoms of the impurity supercells) must lie
/// in [0, 1]. Undefined for a non-square input (caller's responsibility —
/// this is internal C++ plumbing, not a validated public scripting API).
MMatrix computeMMatrix(const EnergyMatrix& energyMatrix, double dilution);

/// Eq. 4 + Eq. 6, evaluated at one composition. `composition` must have the
/// same size as `mMatrix` and its entries should sum to 1 (not enforced);
/// `epsilon` is the paper's zero-division guard (Eq. 6, default 1e-8 as
/// used throughout the paper's Methods section).
double enthalpyOfMixing(const MMatrix& mMatrix, const std::vector<double>& composition,
                        double epsilon = 1e-8);

/// Ideal configurational entropy (Eq. 3), per atom: -k_B * sum(x_i ln x_i),
/// eV/(atom*K). Entries of `composition` at or below 0 are treated as
/// contributing 0 (the x ln x -> 0 limit), matching the paper's convention
/// that S_conf is defined (and finite) all the way to a pure element.
double configurationalEntropyEvPerAtomK(const std::vector<double>& composition);

/// Eq. 1: G_mix = H_mix - T * S_conf, eV/atom.
double gibbsFreeEnergyOfMixingEvPerAtom(const MMatrix& mMatrix,
                                        const std::vector<double>& composition,
                                        double temperatureK);

/// One point of the evaluated binary DeltaH_mix(x) curve.
struct DsimCurvePoint {
    double x = 0.0;               ///< mole fraction of species B
    double enthalpyEvPerAtom = 0.0;
    double enthalpyKjPerMol = 0.0;
};

/// Full result of a binary DSIM analysis: the raw supercell energetics, the
/// two differential mixing enthalpies (dilute-limit slopes), and the
/// interpolated DeltaH_mix(x) curve (Eq. 7) over a composition grid.
struct DsimBinaryResult {
    std::string speciesA;
    std::string speciesB;

    /// Supercell size the four calculations shared (paper: 27 for a 3x3x3
    /// fcc primitive-cell supercell); dilution = 1 / supercellAtomCount.
    int supercellAtomCount = 0;
    double dilution = 0.0;

    /// TOTAL energies (eV) of the four supercells, all the same size
    /// (Eq. 9's E(Z) terms — see the header-level unit-convention note).
    double energyPureATotalEv = 0.0;
    double energyPureBTotalEv = 0.0;
    double energyBInATotalEv = 0.0; ///< E[B][A]: B diluted in host A
    double energyAInBTotalEv = 0.0; ///< E[A][B]: A diluted in host B

    /// M_2[1] (solute B, host A) and M_1[2] (solute A, host B), eV —
    /// Eq. 8-10. These are the tangent-line slopes of the curve below at
    /// x=0 and x=1 respectively (dHdxAt0 = mBInA, dHdxAt1 = -mAInB).
    double mBInA = 0.0;
    double mAInB = 0.0;
    double dHdxAt0 = 0.0;
    double dHdxAt1 = 0.0;

    /// DeltaH_mix(x) (Eq. 7), evaluated on a uniform grid x in [0, 1].
    std::vector<DsimCurvePoint> curve;
};

/// Runs the full binary DSIM pipeline (Eq. 7-10) from four raw TOTAL
/// supercell energies (eV, all four supercells the same atom count — see
/// the unit-convention note above) — the natural entry point once a job's
/// four calculations (pristine A, pristine B, B-in-A, A-in-B) have
/// finished. `compositionPoints` >= 2 sets the resolution of the returned
/// curve (endpoints x=0 and x=1 always included, where DeltaH_mix is
/// exactly 0 by construction of Eq. 7 — not separately measured).
DsimBinaryResult solveDsimBinary(const std::string& speciesA, const std::string& speciesB,
                                 int supercellAtomCount, double energyPureATotalEv,
                                 double energyPureBTotalEv, double energyBInATotalEv,
                                 double energyAInBTotalEv, int compositionPoints = 101);

/// Composition grid on the (N-1)-simplex X_1+...+X_N=1, X_i>=0: every
/// composition with each X_i an integer multiple of 1/resolution — the same
/// construction as oncapintada's `MultiComponentAlloy.simplex_grid()`, used
/// here to evaluate DeltaH_mix over the WHOLE composition space of an
/// N-component alloy (N=3: a triangle; N>3: no direct plot, but the same
/// grid still feeds the M-matrix/pairwise-curve fallback — see
/// DsimScriptGenerator and docs/simulations/dsim.md's "Extensibility"
/// section). Each row sums to exactly 1 (integer arithmetic on multiples of
/// 1/resolution, not accumulated floating-point steps).
std::vector<std::vector<double>> simplexGrid(int nComponents, int resolution);

/// Full result of an N-component DSIM analysis (Eq. 4+6+9-10): the N(N-1)
/// differential mixing enthalpies (M-matrix) plus, for N in {2, 3} only, a
/// directly plottable curve/grid. Building block both DsimScriptGenerator's
/// Python (kept in step by hand) and a future >2-species workflow's C++
/// side share.
struct DsimMulticomponentResult {
    std::vector<std::string> species;
    int supercellAtomCount = 0;
    double dilution = 0.0;
    /// Raw TOTAL supercell energies (eV) — diagonal pristine, off-diagonal
    /// impurity (`energyMatrix[i][j]` = i diluted in host j).
    EnergyMatrix energyMatrix;
    MMatrix mMatrix;

    /// N=2 only: the same fields DsimBinaryResult carries, populated when
    /// species.size() == 2 (empty curve, all-zero scalars otherwise).
    std::vector<DsimCurvePoint> binaryCurve;
    double mBInA = 0.0; ///< M_2[1], only meaningful for N=2
    double mAInB = 0.0; ///< M_1[2], only meaningful for N=2

    /// N=3 only: DeltaH_mix evaluated over the composition triangle
    /// (species[0]=A implicit, xB/xC name species[1]/species[2] — the same
    /// barycentric convention core::TernaryConvexHull already uses).
    struct TernaryGridPoint {
        double xB = 0.0;
        double xC = 0.0;
        double enthalpyEvPerAtom = 0.0;
        double enthalpyKjPerMol = 0.0;
    };
    std::vector<TernaryGridPoint> ternaryGrid;

    /// Every N>=2: the N(N-1)/2 pairwise binary sub-curves (species i vs.
    /// species j, every other species held at 0) — Eq. 7 applied to each
    /// pair in isolation, the fallback view for N>3 (and available for any
    /// N as a cross-section), matching the paper's own Fig. 9c-e
    /// "changes in mixing enthalpy with the addition of one/two elements".
    struct PairwiseCurve {
        int speciesI = 0;
        int speciesJ = 0;
        std::vector<DsimCurvePoint> curve; ///< x = fraction of species j
    };
    std::vector<PairwiseCurve> pairwiseCurves;
};

/// Runs the general N-component DSIM pipeline (Eq. 4+6+9-10) from an N x N
/// TOTAL-energy matrix (see the unit-convention note above). `resolution`
/// sets the N=3 ternary grid's density (points per edge, simplexGrid()'s
/// own parameter) and the N=2/pairwise curves' point count alike.
DsimMulticomponentResult solveDsimMulticomponent(const std::vector<std::string>& species,
                                                 const EnergyMatrix& energyMatrix,
                                                 int supercellAtomCount, int resolution = 20);

/// eV/atom -> kJ/mol (i.e. kJ per mole of ATOMS, the convention every figure
/// in the paper uses). 1 eV = N_A * e / 1000 kJ/mol, the Faraday constant
/// expressed in kJ/(mol*eV); CODATA 2018 exact value. `oncapintada`'s own
/// `constants.kJmol = 96.485364` rounds this in the 6th significant figure
/// (a small, harmless precision mismatch noted in the port report) — this
/// implementation uses the more precise constant.
inline constexpr double kEvToKjPerMol = 96.485332;

/// ## Multi-phase alloys (Fe(bcc)-Co(hcp) and similar)
///
/// The model above assumes both end members share ONE crystal structure
/// (the header doc comment's own "Validity" paragraph). A pair like Fe-Co,
/// where the two elements are stable in DIFFERENT structures, needs one
/// DSIM binary solved per candidate structure — a "bcc branch" and an "hcp
/// branch" — so the two can be compared on one energy scale and the lower
/// one at each composition read off as the stable phase, the same question
/// a CALPHAD lattice-stability diagram answers.
///
/// Each branch is an ORDINARY binary DSIM problem (solveDsimBinary, exactly
/// as above) solved on that phase's own crystal-structure template: the
/// bcc branch's four supercells are bcc Fe, "Co" built by relabeling Fe's
/// bcc template to Co and relaxing, Co-in-bcc-Fe, and Fe-in-(Co-on-bcc);
/// the hcp branch is the symmetric four built from Co's hcp template. Eq. 7
/// makes EVERY such raw branch curve zero at both x=0 and x=1 by
/// construction (the pristine supercells are always its own zero
/// reference) — which is right for a single-lattice alloy, but wrong here:
/// the bcc branch's x=1 endpoint is "Co forced onto bcc", not real
/// (hcp) Co, so it is not the thermodynamic reference the OTHER branch
/// (and the real world) uses for pure Co.
///
/// `applyLatticeStabilityShift` corrects this: it adds a constant,
/// per-atom, linear-in-x offset so the bcc branch's x=1 value lands on
/// hcp Co's own (relaxed, stable) energy rather than bcc-Co's, and
/// symmetrically for the hcp branch's x=0 value against bcc Fe. The shift
/// at each endpoint is exactly the "lattice stability" of that element in
/// the OTHER structure: E(element, wrong structure) - E(element, its own
/// stable structure), eV/atom, always >= 0 when the labeled-stable
/// structure really is lower in energy. `solveDsimMultiPhase` computes both
/// shifts directly from the two branches' own pristine energies (no
/// separate calculation needed — the four pristine supercells already
/// pin down both elements in both structures) and returns both branches,
/// raw and corrected, so the caller (DsimResultsWindow) can plot the
/// corrected pair directly.

/// The constant, linear-in-x correction one phase branch's raw DSIM curve
/// needs to sit on a common (both-phases-comparable) energy reference —
/// see the "Multi-phase alloys" note above. `atX0Ev` is added at x=0 (pure
/// species A), `atX1Ev` at x=1 (pure species B); every intermediate x gets
/// the linear interpolation (1-x)*atX0Ev + x*atX1Ev. A phase native to
/// species A (its own x=0 pristine already IS that element's stable form)
/// has atX0Ev == 0; a phase native to species B has atX1Ev == 0.
struct DsimLatticeStabilityShift {
    double atX0Ev = 0.0;
    double atX1Ev = 0.0;
};

/// Returns `raw` with the lattice-stability shift folded into its curve
/// (`enthalpyEvPerAtom`/`enthalpyKjPerMol` at every point) — a pure
/// re-reference, not a re-fit: `dHdxAt0`/`dHdxAt1` (the tangent-line
/// SLOPES) are unchanged, since adding a function that is linear in x only
/// moves a curve's endpoint VALUES, not its derivative. Every other field
/// (the raw energies, M-values, supercell count) is copied through
/// unchanged — only the plotted curve differs from `raw`.
DsimBinaryResult applyLatticeStabilityShift(const DsimBinaryResult& raw,
                                            const DsimLatticeStabilityShift& shift);

/// The four raw TOTAL supercell energies (eV) one phase branch needs — see
/// solveDsimMultiPhase. All four supercells share the same atom count as
/// the OTHER branch's four (a single `supercellAtomCount`/`dilution` is
/// shared by the whole multi-phase result, matching solveDsimBinary's own
/// fixed-supercell-size convention) — REQUIRED here for a second reason
/// beyond Eq. 9-10's own: the lattice-stability shift divides an energy
/// DIFFERENCE between the two branches by this one shared count (see
/// solveDsimMultiPhase's doc comment), which is only a valid per-atom
/// quantity when both branches' pristine supercells really do hold the
/// same number of atoms. DsimWizard enforces this by refusing to build the
/// two branches when the user's two input cells produce different atom
/// counts at the chosen repeat.
struct DsimPhaseBranchEnergies {
    double pristineATotalEv = 0.0; ///< species A, built on this phase's own template
    double pristineBTotalEv = 0.0; ///< species B, relabeled onto this phase's template + relaxed
    double bInATotalEv = 0.0;      ///< B diluted in host A (this phase's lattice)
    double aInBTotalEv = 0.0;      ///< A diluted in host B-on-this-phase
};

/// One phase branch of a multi-phase DSIM result: which crystal structure
/// it was solved on, the ordinary (zero-at-both-ends) binary DSIM result on
/// that structure, the lattice-stability shift applied, and the corrected
/// (common-reference, directly comparable to the other branch) curve.
struct DsimPhaseResult {
    std::string phaseLabel;
    DsimBinaryResult raw;
    DsimLatticeStabilityShift shift;
    DsimBinaryResult corrected;
};

/// Full multi-phase DSIM result (Fe(bcc)-Co(hcp) and similar): two phase
/// branches of the same A-B pair, each an ordinary binary DSIM solve on its
/// own crystal structure, both re-referenced onto one common energy scale
/// by `applyLatticeStabilityShift` so they can be plotted together and
/// compared directly — see the "Multi-phase alloys" note above.
struct DsimMultiPhaseResult {
    std::string speciesA;
    std::string speciesB;
    /// Native to species A: its own x=0 endpoint is already the stable
    /// element, so phaseA.shift.atX0Ev == 0 (e.g. Fe's bcc).
    DsimPhaseResult phaseA;
    /// Native to species B: phaseB.shift.atX1Ev == 0 (e.g. Co's hcp).
    DsimPhaseResult phaseB;
};

/// Runs the two-branch multi-phase DSIM pipeline: solves an ordinary binary
/// DSIM problem on each phase's own template (solveDsimBinary, unchanged),
/// then computes and applies the lattice-stability shift each needs from
/// the two branches' own pristine energies:
///   shift for phaseA (applied at x=1) = (phaseAEnergies.pristineBTotalEv
///     - phaseBEnergies.pristineBTotalEv) / supercellAtomCount
///   shift for phaseB (applied at x=0) = (phaseBEnergies.pristineATotalEv
///     - phaseAEnergies.pristineATotalEv) / supercellAtomCount
/// (both differences of the SAME element's energy in the two different
/// structures, per atom — see the "Multi-phase alloys" note above). Always
/// >= 0 when `phaseALabel`/`phaseBLabel` really do name each element's own
/// stable structure, as the constructor's example assumes.
DsimMultiPhaseResult solveDsimMultiPhase(const std::string& speciesA, const std::string& phaseALabel,
                                         const DsimPhaseBranchEnergies& phaseAEnergies,
                                         const std::string& speciesB, const std::string& phaseBLabel,
                                         const DsimPhaseBranchEnergies& phaseBEnergies,
                                         int supercellAtomCount, int compositionPoints = 101);

} // namespace calango::core
