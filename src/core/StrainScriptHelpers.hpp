#pragma once

#include "core/StrainVoigt.hpp"

#include <string>
#include <vector>

namespace calango::core {

/// Shared Python-codegen fragments for the two finite-strain modules that
/// apply Calango-precomputed StrainVoigt.hpp deformation gradients to a
/// reference structure: PiezoelectricScriptGenerator (strain -> Berry-phase
/// polarization) and ElasticScriptGenerator (strain -> stress or energy).
/// Everything here is physics-agnostic geometry/bookkeeping — what a
/// strained point's stress or polarization MEANS is entirely the caller's
/// business, kept out of this file on purpose.

/// One Python-literal deformation-gradient matrix, computed HERE in C++
/// (StrainVoigt.hpp) rather than re-derived in the generated script, so the
/// Voigt mapping both modules are unit-tested against (StrainVoigtTest.cpp)
/// is exactly what runs.
std::string strainMatrixLiteral(const Matrix3& f);

/// The +-multiples of a strain magnitude sampled for one Voigt component, in
/// increasing order: 2 points -> {-1, +1}; 4 -> {-2, -1, +1, +2}. 0 is never
/// included here — the zero-strain point is the shared reference,
/// evaluated once and reused by every component, not re-evaluated per
/// component.
std::vector<int> strainSampleMultiples(int pointsPerComponent);

/// `_pbc = list(atoms.pbc); if sum(1 for p in _pbc if p) < 2: raise ...` —
/// refuses a structure with fewer than 2 periodic directions before running
/// anything. `capabilitySubject` names what needs the periodicity in the
/// error message (e.g. "The Berry-phase polarization" or "Computing elastic
/// constants"), grammatically the subject of "... needs periodicity along at
/// least the two in-plane directions".
std::string strainPeriodicityGuardPython(const std::string& capabilitySubject);

/// `VACUUM_AXIS = <axis or None>` / `IS_2D = VACUUM_AXIS is not None`, plus
/// the runtime vacuum-gap confirmation (re-derives the fractional-coordinate
/// gap along the detected axis so a stale or hand-edited config shows up
/// rather than being silently trusted) and a best-effort warning if the
/// baseline's own k-mesh samples more than one point along the vacuum axis.
/// `vacuumAxis` is -1 for a bulk (non-2D) config.
std::string strainVacuumAxisBlockPython(int vacuumAxis);

/// spglib point-group DETECTION only — `point_group` and
/// `point_group_ops_cartesian` (Cartesian rotation/rotoinversion matrices),
/// no refusal of any kind. Centrosymmetric handling (piezoelectricity's
/// exact-zero refusal) is caller-specific and stays out of this shared
/// block; the elastic module has no such refusal at all, since every rank-4
/// elastic tensor is inversion-invariant. `importErrorMessage` names what is
/// skipped if spglib is not installed (e.g. "the centrosymmetric refusal and
/// the tensor symmetrization are both skipped." or "the tensor
/// symmetrization is skipped (forbidden components will not be zeroed).").
std::string strainPointGroupDetectionPython(double symprec, const std::string& importErrorMessage);

/// `def apply_strain(reference, f_matrix): ...` — builds the strained
/// structure via `cell' = cell @ F^T`, `scale_atoms=True` (the CLAMPED-ION
/// convention both modules compute as their base case), and, when `IS_2D`
/// is true at runtime, asserts no atom moved along `VACUUM_AXIS` — the
/// structural guarantee the in-plane-only strain restriction is supposed to
/// provide, checked rather than merely trusted. Depends on `IS_2D` /
/// `VACUUM_AXIS` already being defined in scope (emitted by
/// strainVacuumAxisBlockPython above).
std::string applyStrainFunctionPython();

} // namespace calango::core
