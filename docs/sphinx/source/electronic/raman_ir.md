# Raman and IR spectroscopy

{menuselection}`Electronics --> Raman and IR Spectroscopy…` computes both
vibrational spectra of the Γ-point phonons. They describe the **same modes**
and differ only in which electronic response couples to them:

- **Infrared** intensity is the change in macroscopic *polarization* a mode
  produces,
  $I_\text{IR}(\nu) \propto \bigl|\sum_{\kappa} Z^*_\kappa \cdot
  e_\kappa/\sqrt{M_\kappa}\bigr|^2$. In a periodic crystal there is no
  molecular dipole to differentiate, so the Born effective charges $Z^*$ are
  the only route to it.
- **Raman** activity is the Placzek invariant $45a'^2 + 7\gamma'^2$ built from
  $\partial\alpha/\partial Q$, the change in *polarizability* — the same
  response the {doc}`/electronic/optics` module evaluates, taken in the static
  limit.

---

## One physics core, three engines

Each engine block produces the same four quantities — the Hessian, $Z^*$,
$\partial\alpha/\partial u$, and the geometry — and everything downstream is
**one shared piece of code**: the acoustic sum rule, the mass-weighted
diagonalization, the two contractions, the Stokes prefactor
$(\omega_L - \omega)^4 \cdot [n(\omega)+1]/\omega$, the Lorentzian broadening
and the JSON schema. Imaginary frequencies are reported as *negative*
wavenumbers rather than hidden — an unstable geometry is a physical statement.

The sum rule is not cosmetic: a finite-difference (or finite-tolerance DFPT)
Hessian leaves the acoustic branch floating — measured on MgO the uncorrected
branch came out at 37, 43 and 51 cm⁻¹ — and the Stokes intensity divides by
ω, so a spurious 51 cm⁻¹ "optical" mode plants a peak where the physics has
none. The correction is applied whatever produced the matrix.

What one run of each engine can produce differs by orders of magnitude — this
is the choice the wizard's cost estimate exists to inform:

| Engine | IR half | Raman half |
|---|---|---|
| GPAW | finite displacements: 6N force evaluations (`ase.vibrations`) for the Hessian; **$Z^*$ inherited from a completed Born Effective Charges run** | 6N further SCF runs, each followed by six dielectric evaluations, differencing the static ε tensor |
| VASP | **one DFPT run** — `IBRION = 8` with `LEPSILON = .TRUE.` returns the force constants, every ion's $Z^*$ and $\varepsilon_\infty$ together | no shortcut: VASP computes no Raman tensor, so $\partial\alpha/\partial u$ comes from differencing $\varepsilon_\infty$ over 6N displaced `LEPSILON` runs |
| Quantum ESPRESSO | **one `ph.x` run at q = 0**: `epsil` gives force constants, $Z^*$ and $\varepsilon_\infty$ | the *same* run: `lraman` adds the Raman tensor as an analytic **third-order** response — no displacement amplitude, no linearity assumption |

Engine-specific notes worth knowing before pressing run:

- **GPAW** inherits $Z^*$ rather than recomputing it, because GPAW's own route
  is another 6N Berry-phase runs for a quantity the Raman spectrum never uses.
  Supplying a Born run is what turns the IR column on; without one the phonons
  and the Raman spectrum are computed as usual, **every IR intensity is
  reported as zero, and `ir.computed = false` records which happened** — a
  partial $Z^*$ set, by contrast, is fatal, because it silently zeroes some
  atoms' contribution to every mode. The dielectric evaluations use an NSCF
  step with extra empty bands (4× occupied, floor 24): the response off a bare
  SCF restart misses ~4 % in $\varepsilon_\infty$ on MgO, an error that is
  being *differenced* and lands directly in the Raman tensor. All nine tensor
  components are evaluated (three axes plus three bisector directions) —
  keeping only the diagonal would drop the off-diagonal part of the
  depolarization invariant and understate most modes of a cubic crystal.
- **VASP** needs `NWRITE = 3` to print the second derivatives, `EDIFF = 1e-8`
  (linear response is a derivative — its noise is the SCF's noise amplified)
  and `LREAL = .FALSE.` alongside `LEPSILON`. One parameter set is defined
  once and reused for every displaced run, so nothing can drift between the
  two ε values being differenced.
- **Quantum ESPRESSO** requires fixed occupations (an insulator) and a
  pseudopotential map you must edit: the guess is the conventional
  `<Symbol>.UPF` naming, which a real library rarely matches.

:::{warning}
`lraman` is implemented for **norm-conserving pseudopotentials only**. With an
ultrasoft or PAW set `ph.x` declines rather than approximating, and the run
stops with a message naming that as the cause. The infrared half is
unaffected — turn the Raman spectrum off and the same job still produces it.
:::

---

## Parser hazards, and how the tests pin them

The VASP and QE routes read a Hessian, a $Z^*$ set and a Raman tensor out of
someone else's text format, and **every failure mode in that reading is
silent**:

- VASP's OUTCAR block is $dF/du$ — *minus* the Hessian. Get the sign wrong
  and every mode comes out imaginary: a complete, plausible spectrum drawn at
  negative wavenumbers. The script negates by stated convention *and* checks
  the result against the physics (`verify_hessian_sign`): a structure someone
  chose to take spectra of has at most a few unstable modes, not a majority.
- A `LEPSILON` OUTCAR prints **three dielectric tensors** — excluding local
  field effects, including them, and the ionic contribution. The Raman tensor
  differentiates the second; matching the first (which appears earlier in the
  file) is a tens-of-percent error that lands whole in
  $\partial\alpha/\partial u$.
- QE's `.dyn` file stores force constants in **Ry/bohr², unweighted by the
  masses** (that is why the header carries them); a missing unit conversion
  scales every frequency by ~7.
- `ph.x` writes fixed-point numbers for the dynamical matrix and exponential
  ones for the effective charges, in the same file — and its Raman blocks are
  already $V/4\pi \cdot \partial\varepsilon/\partial u$ in Å², so applying a
  volume factor would scale every activity by the cell volume squared.

`tests/raman_ir_parsers_test.py` extracts the parsers by name from a
*generated* script with `ast` — testing the code that ships, not a
transcription — and drives them with realistic output fragments whose answers
are known by construction (a spring pair whose force constants obey the sum
rule, a $Z^*$ set that obeys the acoustic sum rule).
`tests/raman_ir_math_test.py` does the same for the shared physics functions
against a diatomic with a closed-form answer: a dropped $1/\sqrt{M}$ or a
transposed einsum produces a plausible spectrum, not an error, so "it ran" is
no evidence at all here.

---

## Output and the viewer

All three engines write the same `raman_ir.json`, so one viewer serves them.
The file records which route produced it — `engine` and `method` name the
workflow, and `displacement_A` is **0 for a DFPT result**, because there was
no displacement to report. Per mode: frequency (cm⁻¹ and meV), IR intensity
(e²/amu and (D/Å)²/amu, via 1 eÅ = 4.803204 D), Raman activity (Å⁴/amu),
Stokes intensity, and an acoustic flag; plus the broadened IR and Raman curves
on the requested frequency grid.

:::{note}
The laser wavelength, temperature, broadening and frequency window change only
the *reported* spectrum, not the physics. Mode frequencies and activities are
computed once and the experiment-specific factors applied afterwards — trying
a different laser line does not mean another run; reopen the viewer instead.
:::

% TODO screenshot: Raman/IR viewer with both spectra and the per-mode table, IR column populated from an inherited Born run
```{figure} /_static/img/elec_raman_ir.png
:alt: Raman and IR spectra with the per-mode intensity table
:width: 92%
:figclass: screenshot

Both spectra of the same Γ-point modes; the mode table shows activity and
intensity side by side, with acoustic modes flagged.
```

---

## Limitations

- **Γ-point only.** No phonon dispersion, no LO–TO directional splitting in
  the mode list itself — this module answers "what does the spectrometer
  see", not "what is the full phonon band structure".
- **No resonance Raman.** The Placzek approximation is static; exciting near
  an electronic transition is outside it.
- **IR needs $Z^*$** — on GPAW that means a completed
  {menuselection}`Electronics --> Born Effective Charges…` run
  ({doc}`/electronic/charges`); on VASP/QE it comes with the DFPT run, but
  only for insulators.
- **QE Raman needs norm-conserving pseudopotentials**, as above.
