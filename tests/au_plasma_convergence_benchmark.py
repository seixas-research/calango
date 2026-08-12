#!/usr/bin/env python3
"""Drude / intraband validation on FCC gold, and the k-mesh it needs.

The optics generator emits ``intraband=True, rate="eta"`` for every system.
GPAW gates the free-carrier term on ``self.gs.metallic and intraband``
(``gpaw/response/chi0.py``), so that setting is a no-op on a gapped system and
supplies the Drude response on a metal — which is why the generator does not
try to guess which one it has. This benchmark is the metal half of that claim.

Gold is the right system for it. Its conduction band is a single half-filled
6s band, nearly free-electron-like, so the intraband plasma frequency has a
reference that owes nothing to the calculation:

    hbar*omega_p = hbar*sqrt(n e^2 / (eps_0 m)) = 9.02 eV

for one carrier per atom at a = 4.078 A. In Hartree atomic units that is
sqrt(4*pi*n) with n in electrons/bohr^3 — the same 4*pi that appears in
``chi0_drude.py`` when GPAW accumulates ``plasmafreq_vv``.

Three things are checked, in order of what they would catch:

1. **omega_p converges with the k-mesh, and how.** The plasma frequency is a
   FERMI-SURFACE integral: only partially occupied bands contribute
   (``PlasmaFrequencyIntegrand._band_summation`` returns ``nocc1, nocc2``).
   It therefore converges far more slowly with k than the interband spectrum
   beside it, which is the warning the generated script prints. Measured here,
   point integration falls 16.2 -> 11.8 -> 9.6 eV over 4^3 -> 8^3 -> 12^3 while
   tetrahedron integration is already at 8.8 eV on 8^3. Point integration
   weights the Fermi surface by -df/de, a delta function smeared to the
   occupation width; tetrahedron integration interpolates the bands and
   resolves it at T=0 (``Chi0DrudeCalculator.construct_integral_task_and_wd``).

2. **The tensor is isotropic.** omega_p is a 3x3 tensor, symmetrized over the
   crystal point group. Cubic Au must give omega_p,xx = omega_p,yy = omega_p,zz
   with zero off-diagonals, at EVERY mesh — this holds even where the
   magnitude is badly unconverged, so it catches a broken symmetrization
   independently of sampling.

3. **The term actually reaches epsilon(omega).** With the free-carrier term
   off, Au comes out looking like a dielectric (eps_1 > 0 throughout); with it
   on, eps_1 goes strongly negative below the plasma edge and tracks
   eps_1^interband - omega_p^2/omega^2 to ~1%. This is the assertion that would
   have caught the hardcoded ``intraband=False`` the generator used to emit.

Also pinned: ``rate=0.0`` — ``DielectricFunction``'s OWN default — raises on a
metal, because the rate is what lifts the frequency contour off the real axis
(``assert chi0_drude.zd.upper_half_plane``). That is why the generator passes
``rate="eta"`` rather than leaving the default in place.

Cheap for what it covers: one atom per cell, seconds per mesh.

Skips cleanly (exit 0) without GPAW's response stack.

Usage:  au_plasma_convergence_benchmark.py [--kmax N] [--json PATH]
"""
import argparse
import json
import math
import os
import sys
import tempfile

# --- Gold ------------------------------------------------------------------
LATTICE_A = 4.078        # FCC Au (A)
PW_CUTOFF_EV = 400.0     # 5d states need a real cutoff; 400 eV is converged here
XC = "PBE"
SMEARING_EV = 0.05       # Fermi-Dirac width. Enters point integration directly:
                         # -df/de IS the Fermi-surface weight there.
DRUDE_RATE_EV = 0.1      # only sets the contour; omega_p itself is rate-independent
# hbar in eV*fs — the constant the tau -> rate conversion turns on, kept here
# in the same form the generated script spells it out in.
HBAR_EV_FS = 0.6582119569

# The sweep the plasma frequency is judged on. Ascending; the densest mesh is
# the reference. 4^3 is included precisely because it is wrong — a sweep that
# starts where the answer is already good documents nothing.
KPOINT_SWEEP = (4, 6, 8, 10, 12, 16, 20, 24)

# One 6s carrier per atom, 4 atoms per conventional cell.
CARRIERS_PER_ATOM = 1.0
ATOMS_PER_CONVENTIONAL_CELL = 4

# Tolerances. The free-electron value is a model, not an exact answer: the d
# bands renormalize the 6s mass, so DFT lands a few percent below it (8.8-8.9 eV
# measured). 20 % passes that comfortably while still failing a term that
# returns 0 (intraband silently disabled) or the 16 eV of an unconverged mesh.
PLASMA_TOLERANCE = 0.20
# Isotropy is checked on the omega_p^2 tensor — the object GPAW actually
# symmetrizes — and RELATIVE to its diagonal. Checking sqrt'd off-diagonals
# instead would flag pure float noise: an off-diagonal 1e-6 of the diagonal in
# omega_p^2 becomes a 1e-2 eV number once square-rooted, which reads alarming
# and means nothing.
#
# Point integration lands on exact isotropy (spread ~1e-16, the tensor is a
# weighted sum over symmetry-equivalent points). Tetrahedron integration does
# not: its Delaunay tessellation of the zone is not itself cubic, so the three
# diagonal entries differ in the last few digits — 1.3e-4 measured at 6^3.
# A decade above that still fails a broken symmetrization by three orders.
ISOTROPY_TOLERANCE = 1e-3      # spread of the diagonal / its mean
OFFDIAG_TOLERANCE = 1e-3       # max |off-diagonal| / mean diagonal


def _bootstrap_gpaw_env() -> None:
    """Re-exec under the GPAW conda env from ~/.calango/settings.json when the
    current interpreter (e.g. CTest's embedded python) has no GPAW, so the
    benchmark really runs.  No-op if GPAW is importable or no env is configured
    (main() then SKIPs cleanly)."""
    try:
        import gpaw  # noqa: F401
        return
    except Exception:
        pass
    if os.environ.get("_CALANGO_GPAW_REEXEC"):
        return
    envs = []
    try:
        with open(os.path.expanduser("~/.calango/settings.json")) as fh:
            jobs = json.load(fh).get("jobs", {})
        presets = json.loads(jobs.get("environmentPresets", "{}") or "{}")
        if presets.get("GPAW"):
            envs.append(presets["GPAW"])
        if jobs.get("environmentPath"):
            envs.append(jobs["environmentPath"])
    except Exception:
        return
    for env in envs:
        for py in (os.path.join(env, "bin", "python3"),
                   os.path.join(env, "bin", "python")):
            if os.path.isfile(py) and \
                    os.path.realpath(py) != os.path.realpath(sys.executable):
                os.environ["_CALANGO_GPAW_REEXEC"] = "1"
                os.execv(py, [py, os.path.abspath(__file__)] + sys.argv[1:])


def free_electron_plasma_frequency_ev() -> float:
    """hbar*omega_p for CARRIERS_PER_ATOM free carriers per Au atom, in eV.

    Computed in Hartree atomic units, where omega_p^2 = 4*pi*n with n the
    carrier density in electrons/bohr^3. That is deliberately the same 4*pi
    GPAW accumulates in ``chi0_drude.py``, so the reference and the quantity
    it is compared against share a convention instead of being reconciled by
    a factor nobody re-derives.
    """
    from ase.units import Bohr, Hartree
    a_bohr = LATTICE_A / Bohr
    density = (CARRIERS_PER_ATOM * ATOMS_PER_CONVENTIONAL_CELL) / a_bohr ** 3
    return math.sqrt(4.0 * math.pi * density) * Hartree


def plasma_frequency_tensor(gpw, integrationmode, qsymmetry=True):
    """The 3x3 omega_p^2 tensor (eV^2) from a converged ground state.

    omega_p^2 rather than omega_p: that is what GPAW builds and symmetrizes,
    and an elementwise square root of a tensor is not a tensor — the
    off-diagonals of the root have no meaning, while the off-diagonals of
    omega_p^2 are exactly what the isotropy check is about.

    Goes at ``Chi0DrudeCalculator`` directly rather than scraping the
    "Plasma frequency:" line out of the response log: the tensor is the
    quantity being validated, and parsing a rounded print of it would throw
    away the off-diagonals the isotropy check needs.

    Returns (tensor_eV, qsymmetry_used). On the IBZ-vertex rejection it retries
    with symmetry reduction disabled — the same ladder the generated optics
    script walks, for the same reason: qsymmetry is what carries the vertex
    requirement, so dropping it keeps the integrator that was asked for.
    """
    import numpy as np
    from ase.units import Hartree
    from gpaw.mpi import world
    from gpaw.response.chi0_drude import Chi0DrudeCalculator
    from gpaw.response.frequencies import FrequencyGridDescriptor
    from gpaw.response.pair import get_gs_and_context

    def _run(qsym):
        gs, context = get_gs_and_context(gpw, None, world, None)
        drude = Chi0DrudeCalculator(gs, context, qsymmetry=qsym,
                                    integrationmode=integrationmode)
        # omega_p does not depend on this grid — only the Drude chi does — but
        # `calculate` needs one, and the rate must be non-zero for the contour
        # assertion inside it.
        data = drude.calculate(FrequencyGridDescriptor([0.0]), DRUDE_RATE_EV)
        # plasmafreq_vv holds omega_p^2 in Hartree^2, 4*pi included.
        return data.plasmafreq_vv.real * Hartree ** 2

    try:
        return _run(qsymmetry), qsymmetry
    except ValueError as exc:
        if "vertices of the IBZ" not in str(exc) or not qsymmetry:
            raise
        return _run(False), False


def ground_state(atoms, k, workdir):
    """SCF at one mesh, written to a .gpw. Gamma-centred: tetrahedron
    integration requires it, and holding it fixed keeps the two integrators
    reading the same ground states."""
    from gpaw import GPAW, PW, FermiDirac
    path = os.path.join(workdir, f"au_{k}.gpw")
    atoms.calc = GPAW(mode=PW(PW_CUTOFF_EV), xc=XC,
                      kpts={"size": (k, k, k), "gamma": True},
                      occupations=FermiDirac(SMEARING_EV),
                      txt=os.path.join(workdir, f"gs_{k}.txt"))
    atoms.get_potential_energy()
    atoms.calc.write(path, mode="all")
    return path


def check_isotropy(tensor_sq, label):
    """Cubic Au: omega_p^2 must be a multiple of the identity.

    Takes the omega_p^2 tensor (eV^2) and returns the scalar omega_p (eV).
    Holds at every mesh, including the coarse ones where the MAGNITUDE is
    badly wrong — the point group does not care how well the Fermi surface is
    sampled — so this catches a broken symmetrization on its own, without
    waiting for convergence.
    """
    import numpy as np
    diag = np.diag(tensor_sq)
    mean = float(diag.mean())
    assert mean > 0.0, f"{label}: omega_p^2 has non-positive trace {diag}"
    spread = float(diag.max() - diag.min()) / mean
    offdiag = float(np.abs(tensor_sq - np.diag(diag)).max()) / mean
    assert spread <= ISOTROPY_TOLERANCE, (
        f"{label}: omega_p^2 is not isotropic on cubic Au — diagonal "
        f"{diag} eV^2 spans {spread:.2e} of its mean")
    assert offdiag <= OFFDIAG_TOLERANCE, (
        f"{label}: omega_p^2 has non-zero off-diagonals on cubic Au — "
        f"max |off-diagonal| is {offdiag:.2e} of the diagonal")
    return math.sqrt(mean)


def sweep(atoms, meshes, workdir):
    """omega_p against the k-mesh, both integrators."""
    records = []
    for k in meshes:
        gpw = ground_state(atoms, k, workdir)
        record = {"kpts": [k, k, k], "k_per_axis": k}
        for mode, key in (("point integration", "point"),
                          ("tetrahedron integration", "tetrahedron")):
            try:
                tensor, qsym = plasma_frequency_tensor(gpw, mode)
            except Exception as exc:
                # One integrator failing at one mesh must not lose the curve.
                record[key] = None
                record[f"{key}_error"] = f"{type(exc).__name__}: {exc}"
                print(f"  k={k:2d} {mode:24s} FAILED: "
                      f"{type(exc).__name__}: {str(exc)[:60]}")
                continue
            omega_p = check_isotropy(tensor, f"k={k}, {mode}")
            record[key] = omega_p
            record[f"{key}_qsymmetry"] = bool(qsym)
            note = "" if qsym else "  (qsymmetry=False)"
            print(f"  k={k:2d} {mode:24s} omega_p = {omega_p:6.3f} eV{note}")
        records.append(record)
    return records


def check_epsilon(gpw, omega_p_ev, workdir):
    """The free-carrier term reaching epsilon(omega), and the ladder around it.

    Compares eps_1 with the term on and off, and against the closed Drude form
    eps_1^interband - omega_p^2/omega^2. Evaluated at omega >= 1 eV, where the
    relaxation rate is a small correction — GPAW implements the term as
    omega_p^2/(omega + i*rate)^2, so near omega ~ rate the two expressions
    legitimately part company.
    """
    import numpy as np
    from gpaw import GPAW
    from gpaw.response.df import DielectricFunction

    gs = GPAW(gpw, txt=None)
    n_occ = max(1, int(round(gs.get_number_of_electrons() / 2.0)))
    nscf = gs.fixed_density(nbands=n_occ + 24,
                            convergence={"bands": n_occ + 12},
                            txt=os.path.join(workdir, "nscf.txt"))
    nscf_path = os.path.join(workdir, "nscf.gpw")
    nscf.write(nscf_path, mode="all")

    frequencies = np.linspace(0.05, 6.0, 60)

    def epsilon(intraband, rate):
        df = DielectricFunction(
            nscf_path, frequencies=frequencies, eta=0.1,
            # A linear frequency grid is not a Hilbert grid; GPAW asserts on
            # the mismatch rather than substituting one.
            hilbert=False,
            intraband=intraband, rate=rate,
            integrationmode="point integration", txt=None)
        _, eps = df.get_dielectric_function(direction="x", filename=None)
        return (np.asarray(df.get_frequencies(), dtype=float),
                np.asarray(eps).real)

    omega, eps1_inter = epsilon(False, 0.0)
    _, eps1_full = epsilon(True, "eta")

    # 1. Without the term Au looks like a dielectric at LOW frequency — the
    #    bug that was there. Deliberately not a statement about the whole
    #    window: interband eps_1 is entitled to go negative higher up (the
    #    f-sum rule drives it below 1 and past zero above a strong absorption
    #    band), and an earlier version of this test asserted on the global
    #    minimum, which held at 24^3 and failed at 8^3 for a reason that had
    #    nothing to do with the free-carrier term.
    probe = int(np.argmin(np.abs(omega - 1.0)))
    assert float(eps1_inter[probe]) > 0.0, (
        f"with intraband=False, eps_1({omega[probe]:.2f} eV) = "
        f"{eps1_inter[probe]:.2f} should be positive on Au below the "
        "interband onset — that is the very error being guarded against")

    # 2. With it, eps_1 is strongly negative below the plasma edge.
    assert float(eps1_full[probe]) < -10.0, (
        f"with intraband=True, eps_1({omega[probe]:.2f} eV) = "
        f"{eps1_full[probe]:.2f} is not the negative free-carrier response a "
        "metal must show")

    # 3. And it is quantitatively the Drude term, not merely something negative.
    deviations = []
    for i, w in enumerate(omega):
        if w < 1.0:
            continue
        predicted = eps1_inter[i] - omega_p_ev ** 2 / w ** 2
        if abs(predicted) < 1.0:      # the crossing, where a ratio is meaningless
            continue
        deviations.append(abs(predicted - eps1_full[i]) / abs(predicted))
    worst = max(deviations)
    # 3.0 % measured at 24^3. The residual is the relaxation rate: GPAW uses
    # omega_p^2/(omega + i*rate)^2 where the comparison uses omega_p^2/omega^2,
    # so it shrinks as omega/rate grows and never reaches zero.
    assert worst < 0.10, (
        f"eps_1 does not follow eps_1^interband - omega_p^2/omega^2: worst "
        f"relative deviation {worst:.1%} above 1 eV")

    # 4. rate=0.0 — DielectricFunction's own default — is not usable on a metal.
    rate_zero_raised = False
    try:
        epsilon(True, 0.0)
    except Exception:
        rate_zero_raised = True
    assert rate_zero_raised, (
        "rate=0.0 with intraband=True was expected to fail on a metal "
        "(assert chi0_drude.zd.upper_half_plane); it did not, so the "
        'generator\'s rate="eta" may no longer be load-bearing')

    print(f"  eps_1(1 eV): {eps1_inter[probe]:8.2f} without the Drude term, "
          f"{eps1_full[probe]:9.2f} with it")
    print(f"  Drude form eps_1^inter - omega_p^2/omega^2 holds to "
          f"{worst:.1%} above 1 eV")
    print("  rate=0.0 raises on a metal, as the generator's comment states")
    return {"omega_eV": [float(w) for w in omega],
            "eps1_interband": [float(v) for v in eps1_inter],
            "eps1_with_drude": [float(v) for v in eps1_full],
            "drude_form_worst_deviation": float(worst)}


def check_relaxation_time(nscf_path, workdir):
    """The relaxation time behaves like one.

    The wizard collects tau (fs) and the generator converts it to GPAW's rate
    as hbar/(2*tau) — the factor of two being GPAW's convention, since it damps
    as omega_p^2/(omega + i*rate)^2 where the textbook form is
    omega_p^2/(omega*(omega + i*Gamma)) with Gamma = hbar/tau.

    For rate << omega the free-carrier part of eps_2 goes as
    2 omega_p^2 rate / omega^3, i.e. PROPORTIONAL TO 1/tau. So doubling tau
    must halve it. That is a scaling law, not a fitted number: a conversion
    that dropped the two, or passed tau through as if it were a rate, fails it
    — and unlike an absolute value it cannot be satisfied by accident.
    """
    import numpy as np
    from gpaw.response.df import DielectricFunction

    frequencies = np.linspace(0.05, 6.0, 60)

    def eps1_eps2(intraband, rate):
        df = DielectricFunction(
            nscf_path, frequencies=frequencies, eta=0.1, hilbert=False,
            intraband=intraband, rate=rate,
            integrationmode="point integration", txt=None)
        _, eps = df.get_dielectric_function(direction="x", filename=None)
        return (np.asarray(df.get_frequencies(), dtype=float),
                np.asarray(eps))

    # Interband-only reference, so the free-carrier part can be isolated by
    # subtraction rather than assumed to dominate.
    omega, eps_inter = eps1_eps2(False, 0.0)
    probe = int(np.argmin(np.abs(omega - 2.0)))

    ratios = []
    previous = None
    for tau_fs in (5.0, 10.0, 20.0, 40.0):
        rate = HBAR_EV_FS / (2.0 * tau_fs)
        _, eps = eps1_eps2(True, rate)
        drude = float(eps[probe].imag - eps_inter[probe].imag)
        assert drude > 0.0, (
            f"tau={tau_fs} fs: the free-carrier part of eps_2 came out "
            f"{drude:.4f}, which is not absorption")
        if previous is not None:
            ratios.append(previous / drude)
        previous = drude

    worst = max(abs(r - 2.0) for r in ratios)
    assert worst < 0.05, (
        "the free-carrier eps_2 does not scale as 1/tau — measured ratios "
        f"{['%.3f' % r for r in ratios]} against the expected 2.000, worst "
        f"deviation {worst:.3f}. Check the hbar/(2*tau) conversion.")
    print(f"  eps_2 at {omega[probe]:.2f} eV halves when tau doubles: "
          f"ratios {', '.join('%.3f' % r for r in ratios)} (expected 2.000)")
    return {"probe_eV": float(omega[probe]),
            "tau_doubling_ratios": [float(r) for r in ratios]}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--kmax", type=int, default=max(KPOINT_SWEEP),
                        help="largest k-points-per-axis to evaluate")
    parser.add_argument("--json", default=None,
                        help="write the convergence table here")
    args = parser.parse_args()

    try:
        import numpy as np
        from ase.build import bulk
        from gpaw import GPAW, PW, FermiDirac  # noqa: F401
        from gpaw.response.chi0_drude import Chi0DrudeCalculator  # noqa: F401
        from gpaw.response.df import DielectricFunction  # noqa: F401
    except Exception as exc:
        print(f"SKIP: GPAW response stack not available ({exc})")
        return 0

    meshes = [k for k in KPOINT_SWEEP if k <= args.kmax]
    if not meshes:
        print(f"SKIP: no mesh in {KPOINT_SWEEP} at or below --kmax {args.kmax}")
        return 0

    reference = free_electron_plasma_frequency_ev()
    workdir = tempfile.mkdtemp(prefix="calango_au_plasma_")
    cwd = os.getcwd()
    os.chdir(workdir)
    try:
        atoms = bulk("Au", "fcc", a=LATTICE_A)
        print(f"FCC Au, a = {LATTICE_A} A, PW({PW_CUTOFF_EV:.0f} eV), {XC}, "
              f"Fermi-Dirac {SMEARING_EV} eV")
        print(f"Free-electron reference ({CARRIERS_PER_ATOM:g} carrier/atom): "
              f"hbar*omega_p = {reference:.3f} eV\n")
        print("Plasma frequency against k-mesh:")
        records = sweep(atoms, meshes, workdir)

        # --- The convergence table, which is the point of the test ---------
        print("\n  mesh        point int.   tetrahedron    (eV)")
        for r in records:
            point = r.get("point")
            tetra = r.get("tetrahedron")
            print(f"  {r['k_per_axis']:2d}^3     "
                  f"{'   n/a  ' if point is None else f'{point:8.3f}'}     "
                  f"{'   n/a  ' if tetra is None else f'{tetra:8.3f}'}")

        # --- Verification ---------------------------------------------------
        tetra_values = [r["tetrahedron"] for r in records
                        if r.get("tetrahedron") is not None]
        point_values = [r["point"] for r in records if r.get("point") is not None]
        assert point_values, "point integration produced no plasma frequency"
        assert tetra_values, "tetrahedron integration produced no plasma frequency"

        # The failure mode with no symptom: intraband quietly contributing
        # nothing. Every mesh, both integrators, must give a real carrier
        # density — a zero here is the whole bug class.
        for r in records:
            for key in ("point", "tetrahedron"):
                value = r.get(key)
                assert value is None or value > 1.0, (
                    f"omega_p = {value} eV at {r['k_per_axis']}^3 ({key}) — a "
                    "vanishing plasma frequency means the intraband term "
                    "contributed nothing")

        converged = tetra_values[-1]
        deviation = abs(converged - reference) / reference
        assert deviation <= PLASMA_TOLERANCE, (
            f"converged omega_p = {converged:.3f} eV is {deviation:.1%} from "
            f"the free-electron {reference:.3f} eV, beyond the "
            f"{PLASMA_TOLERANCE:.0%} tolerance")

        # Tetrahedron integration resolves the Fermi surface at T=0, point
        # integration smears it to the occupation width. The claim is about the
        # COARSE end, where they must disagree, and they must then meet: if the
        # two never converged to each other one of them would be wrong.
        assert point_values[0] > tetra_values[0], (
            "on the coarsest mesh point integration is expected to overshoot "
            f"tetrahedron integration ({point_values[0]:.3f} vs "
            f"{tetra_values[0]:.3f} eV); it did not, so the two integrators no "
            "longer differ the way the Fermi-surface argument says they must")
        if len(records) > 2 and records[-1].get("point") is not None:
            gap = abs(records[-1]["point"] - records[-1]["tetrahedron"])
            coarse = abs(records[0]["point"] - records[0]["tetrahedron"])
            assert gap < coarse, (
                "the two integrators do not approach each other with mesh "
                f"density ({coarse:.3f} eV apart at {records[0]['k_per_axis']}^3, "
                f"{gap:.3f} eV at {records[-1]['k_per_axis']}^3)")

        print(f"\n  converged omega_p = {converged:.3f} eV "
              f"({deviation:.1%} from free-electron {reference:.3f} eV)")

        # --- The term inside epsilon(omega) --------------------------------
        print("\nFree-carrier term in epsilon(omega):")
        dense = max(meshes)
        # The omega_p the closed Drude form is checked against must be the one
        # that actually went into this epsilon. DielectricFunction is built
        # below with point integration, so it is the POINT value at this mesh
        # — not the tetrahedron value converged above, which is a better number
        # for a different integrator and would leave a residual that looks like
        # a modelling error rather than the mismatch it is.
        drude_omega_p = records[-1].get("point") or converged
        spectrum = check_epsilon(os.path.join(workdir, f"au_{dense}.gpw"),
                                 drude_omega_p, workdir)

        # The relaxation time, on the NSCF check_epsilon just wrote.
        print("\nDrude relaxation time:")
        relaxation = check_relaxation_time(os.path.join(workdir, "nscf.gpw"),
                                           workdir)

        payload = {
            "system": {"formula": "Au", "structure": "fcc",
                       "lattice_a_A": LATTICE_A, "xc": XC,
                       "pw_cutoff_eV": PW_CUTOFF_EV,
                       "smearing_eV": SMEARING_EV},
            "reference": {"free_electron_omega_p_eV": reference,
                          "carriers_per_atom": CARRIERS_PER_ATOM},
            "converged": {"omega_p_eV": converged,
                          "kpts": records[-1]["kpts"],
                          "integration": "tetrahedron",
                          "deviation_from_free_electron": deviation},
            "points": records,
            "epsilon": spectrum,
            "relaxation_time": relaxation,
        }
        if args.json:
            target = args.json if os.path.isabs(args.json) \
                else os.path.join(cwd, args.json)
            with open(target, "w") as fh:
                json.dump(payload, fh, indent=2)
            print(f"\nwrote {target}")

        print(f"\nPASS: Au plasma frequency converges to {converged:.3f} eV "
              f"over {meshes[0]}^3..{dense}^3; free-carrier term verified in "
              "epsilon(omega).")
        return 0
    finally:
        os.chdir(cwd)


if __name__ == "__main__":
    _bootstrap_gpaw_env()
    sys.exit(main())
