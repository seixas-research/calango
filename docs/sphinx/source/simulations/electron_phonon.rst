Electron-phonon coupling
========================

:menuselection:`Simulation --> Electron-Phonon Coupling` computes the
electron-phonon matrix elements :math:`g_{mn}^{\nu}(\mathbf{k},\mathbf{q})` by
supercell finite differences, using GPAW's ``gpaw.elph`` module, and derives
from them the quantities that make the coupling usable: the Eliashberg
spectral function :math:`\alpha^2F(\omega)`, the coupling constant
:math:`\lambda`, and the **electron-phonon relaxation time** :math:`\tau`
which the Drude term of the :doc:`optics module </electronic/optics>` takes as
its input.

The module is GPAW-only, and locked that way rather than merely defaulted.
Every other engine's ASE calculator exposes energies and forces; this method
needs the change in the *effective potential* under an atomic displacement,
which is not part of that interface.

.. note::

   This is a metallic property. Both electron delta functions in
   :math:`\alpha^2F` sit at the Fermi level, so a gapped system has nothing to
   integrate — the run detects a vanishing density of states at
   :math:`E_\mathrm{F}` and says so rather than returning a meaningless zero.

Theory
------

The coupling is defined as the matrix element of the potential change caused
by a phonon:

.. math::

   g_{mn}^{\nu}(\mathbf{k},\mathbf{q})
     = \sqrt{\frac{\hbar}{2 M \omega_{\mathbf{q}\nu}}}\,
       \langle m,\mathbf{k}+\mathbf{q} |\;
       \boldsymbol{\epsilon}_{\mathbf{q}\nu}\cdot\nabla V \;|
       n,\mathbf{k} \rangle ,

where :math:`\boldsymbol{\epsilon}_{\mathbf{q}\nu}` is the eigenvector of mode
:math:`\nu` at wavevector :math:`\mathbf{q}` and the prefactor is the
zero-point displacement amplitude. With that prefactor included the elements
carry units of energy (eV), which is what the spectral function below is
defined for.

:math:`\nabla V` is obtained by finite differences: every atom of the cell is
displaced by :math:`\pm\delta` along :math:`x`, :math:`y` and :math:`z` inside
a supercell, and the resulting change in the self-consistent effective
potential is stored. This is **6N+1 supercell SCF runs** and is essentially the
entire cost of the module.

The Fermi-surface average of :math:`|g|^2`, resolved by phonon frequency, is
the Eliashberg spectral function:

.. math::

   \alpha^2F(\omega) = \frac{1}{N(E_\mathrm{F})}
     \sum_{\mathbf{q}\nu}\sum_{\mathbf{k}mn}
     |g_{mn}^{\nu}(\mathbf{k},\mathbf{q})|^2\,
     \delta(\varepsilon_{n\mathbf{k}} - E_\mathrm{F})\,
     \delta(\varepsilon_{m\mathbf{k}+\mathbf{q}} - E_\mathrm{F})\,
     \delta(\omega - \omega_{\mathbf{q}\nu}) .

Its first inverse moment is the mass-enhancement coupling constant, the single
number the whole calculation reduces to for transport purposes:

.. math::

   \lambda = 2\int_0^{\infty} \frac{\alpha^2F(\omega)}{\omega}\,\mathrm{d}\omega .

Relaxation time and the Drude model
-----------------------------------

In the high-temperature limit the phonon-limited scattering rate follows
Allen's result:

.. math::

   \frac{\hbar}{\tau} = 2\pi\lambda k_\mathrm{B}T ,
   \qquad k_\mathrm{B}T \gg \hbar\omega_{\log} .

Two caveats are stated by the run itself rather than left to the reader,
because both change the number rather than merely qualifying it:

* This uses the **mass-enhancement** :math:`\lambda`, not the transport
  :math:`\lambda_{\mathrm{tr}}`. They differ by the
  :math:`(1-\cos\theta)` backscattering weight in the Fermi-surface average.
  For a free-electron-like metal (Al, Na) they agree to a few percent; for a
  transition metal with an anisotropic Fermi surface they need not.
* The formula is the :math:`T \gg \Theta_\mathrm{D}` limit. Below roughly
  :math:`\Theta_\mathrm{D}/3` the true rate falls off as :math:`T^5`
  (Bloch-Grüneisen) and this **overestimates** it. The run compares the
  requested temperature against :math:`\omega_{\log}/k_\mathrm{B}` and warns
  when the limit does not apply.

The handoff to the optics module carries one trap worth naming. GPAW's
``DielectricFunction`` implements the Drude term as
:math:`\omega_p^2/(\omega + i\,\mathrm{rate})^2`, whereas the textbook form is
:math:`\omega_p^2/(\omega(\omega + i\Gamma))` with :math:`\Gamma = \hbar/\tau`.
Matching the two gives :math:`\Gamma = 2\,\mathrm{rate}`, so the value GPAW
wants is

.. math::

   \mathrm{rate} = \frac{\hbar}{2\tau} .

``epc.json`` reports **both** :math:`\tau` and this rate for that reason. In
the Optics wizard you enter :math:`\tau` directly and it applies the factor of
two itself — see :doc:`/electronic/optics`.

Setting it up
-------------

.. figure:: /_static/img/sim_electron_phonon_wizard.png
   :alt: Electron-Phonon Coupling wizard

   The Electron-Phonon Settings stage.

**Finite Displacements**

.. list-table::
   :header-rows: 1
   :widths: 30 15 55

   * - Setting
     - Default
     - Notes
   * - :guilabel:`Supercell`
     - 2×2×2
     - The *range* of the interaction the calculation can represent, and the
       bound on which q-points exist at all.
   * - :guilabel:`Displacement δ`
     - 0.01 Å
     - Trades truncation error (anharmonicity) against numerical noise.
   * - :guilabel:`LCAO basis`
     - ``dzp``
     - Not a mode choice — the projection stage needs basis functions, so
       there is no plane-wave path.

**Electron and Phonon Meshes**

.. list-table::
   :header-rows: 1
   :widths: 30 15 55

   * - Setting
     - Default
     - Notes
   * - :guilabel:`Electron k-mesh`
     - 8×8×8
     - A Fermi-surface integral, so it converges as slowly as the plasma
       frequency does. Must be a multiple of the q-mesh.
   * - :guilabel:`Phonon q-mesh`
     - 2×2×2
     - Bounded above by the supercell; a denser q-mesh means a bigger
       supercell, not just more points.

Two conditions are checked **live in the wizard** and again in the first
seconds of the run, because otherwise GPAW discovers them only after the
displacement runs have been paid for:

#. every :math:`\mathbf{q}` must be commensurate with the supercell — a
   :math:`\mathbf{q}` that is not a reciprocal vector of it has no dynamical
   matrix in the finite-difference cache at any price;
#. the k-mesh must be a multiple of the q-mesh, so that every
   :math:`\mathbf{k}+\mathbf{q}` is itself a sampled state with an eigenvalue
   to evaluate the second delta at.

The wizard's summary line restates the cost (``6N+1`` runs on a supercell
:math:`n\times` the cell) and turns red naming the offending axis when either
condition fails.

**Spectral Function and Relaxation Time**

There is **no Fermi-surface smearing setting**, and that is deliberate. The two
:math:`\delta(\varepsilon - E_\mathrm{F})` factors are integrated with the
**linear tetrahedron method**, which interpolates the bands inside each
tetrahedron instead of requiring states to land near :math:`E_\mathrm{F}`, and
so has no width to choose. The accuracy is set by the k-mesh alone.

.. note::

   Earlier versions did expose a Gaussian width here, and it was not a
   cosmetic setting: on fcc aluminium at a :math:`6^3` mesh, :math:`\lambda`
   ran from 0.009 to 31 as that width was varied by a factor of sixteen, with
   no plateau anywhere. The reported number was whichever width happened to be
   configured. Removing the parameter — rather than documenting how to
   converge it — is what fixed that.

:guilabel:`Phonon smearing` (0.005 eV) bins the modes into
:math:`\alpha^2F(\omega)` **for display only**. :math:`\lambda` is summed
over the modes exactly and does not change with it.
:guilabel:`Temperature` (300 K) is where :math:`\tau` is reported, since
:math:`\tau` is temperature-dependent by physics rather than by preference.

:guilabel:`Coulomb` :math:`\mu^*` (0.10) is the Morel--Anderson pseudopotential
used for the superconducting :math:`T_\mathrm{c}` estimate. It is **empirical**
--- nothing in this program computes it, and nothing can: it screens the direct
Coulomb repulsion and is conventionally 0.10--0.15 for sp metals and higher for
transition metals.

.. warning::

   :math:`T_\mathrm{c}` depends on :math:`\mu^*` *exponentially*. For
   aluminium at its literature :math:`\lambda = 0.43` and
   :math:`\omega_{\log} = 296` K, the swept curve reads

   .. list-table::
      :header-rows: 1
      :widths: 40 20 20 20

      * - :math:`\mu^*`
        - 0.08
        - 0.12
        - 0.16
      * - :math:`T_\mathrm{c}`
        - 2.71 K
        - 1.19 K
        - 0.37 K

   --- a factor of **7.5** across a range every value in which is entirely
   defensible, against a measured 1.18 K. Quote the range, never a single
   value.

Outputs
-------

The GPAW run itself stops at the raw matrix elements, writing
``elph_raw.txt`` (a manifest naming the mesh) alongside the eigenvalues,
phonon frequencies, :math:`\mathbf{k}+\mathbf{q}` map and GPAW's own
``gsqklnn.npy``. Calango then integrates them and writes ``epc.json``.

That split exists because :math:`|g|^2` is
(spins, q, k, modes, bands, bands) complex, which reaches tens of gigabytes on
a production mesh — so the analysis is designed to run *beside* the
calculation rather than after a download. When a run finishes inside Calango
this happens automatically. For a run on a cluster, do it in place:

.. code-block:: console

   $ calango-elph-analyze /path/to/run
   lambda            0.4132
   omega_log         24.87 meV
   N(E_F)            0.2375 states/eV per cell per spin
   temperature       300 K
   tau               9.812 fs
   hbar/tau          0.06708 eV
   Drude rate        0.03354 eV  (= hbar/2tau, ...)

   wrote /path/to/run/epc.json

``epc.json`` holds:

.. list-table::
   :header-rows: 1
   :widths: 35 65

   * - Key
     - Meaning
   * - ``omega_eV`` / ``alpha2F``
     - The spectral function :math:`\alpha^2F(\omega)` on its frequency grid.
   * - ``lambda``
     - The coupling constant :math:`\lambda`.
   * - ``omega_log_eV``
     - Logarithmic average phonon frequency :math:`\omega_{\log}`.
   * - ``relaxation_time_fs``
     - :math:`\tau` at the requested temperature, femtoseconds.
   * - ``scattering_rate_eV``
     - :math:`\hbar/\tau` in eV.
   * - ``drude_rate_eV``
     - :math:`\hbar/2\tau_{tr}` — the number GPAW's ``rate`` parameter takes.
       Built on the **transport** lifetime whenever band velocities were
       available, because a Drude term describes how a *current* decays. A run
       that had to fall back to the mass-enhancement :math:`\tau` — no
       velocities, so no :math:`1-\cos\theta` weight — says so in
       ``warnings`` rather than quietly handing the optics module a Drude peak
       of the wrong width.
   * - ``dos_at_fermi``
     - :math:`N(E_\mathrm{F})`, the normalizing density of states.
   * - ``superconductivity``
     - Nested block: ``tc_allen_dynes_K`` (corrected, the number to quote),
       ``tc_allen_dynes_uncorrected_K``, ``tc_mcmillan_K``, ``f1``, ``f2``,
       ``gap_meV``, ``gap_ratio``, the ``mu_star`` used, and its own ``ok``
       and ``warnings``. ``ok`` false means the material is not a
       phonon-mediated superconductor at this coupling --- a result about the
       material, not a failure of the calculation around it.
   * - ``lambda_per_mode`` / ``linewidths_eV``
     - :math:`\lambda_{q\nu}` and :math:`\gamma_{q\nu}`, indexed as the
       phonon frequencies.
   * - ``lambda_transport`` / ``relaxation_time_transport_fs``
     - :math:`\lambda_{tr}` and :math:`\tau_{tr}`, with
       ``alpha2F_transport`` on the same frequency grid as ``alpha2F``.
   * - ``resistivity_micro_ohm_cm``
     - :math:`\rho` at the requested temperature. Zero when no plasma
       frequency was supplied.
   * - ``occupied_bandwidth_eV`` / ``retardation_log``
     - :math:`E_\mathrm{F} - \min(\varepsilon)` and
       :math:`\ln(W/\omega_{\log})`, for converting a bare :math:`\mu`
       into :math:`\mu^*`.
   * - ``omega_bar2_eV`` / ``mass_enhancement``
     - :math:`\bar\omega_2` and :math:`1 + \lambda`.
   * - ``integration``
     - ``"tetrahedron"``. Recorded because a tetrahedron :math:`\lambda` and a
       smeared one are not comparable numbers, and a stored result with no
       method attached cannot be read a year later.
   * - ``excluded_modes``
     - Count of imaginary frequencies. Non-zero means the structure is not at
       a local minimum; those modes are excluded from :math:`\alpha^2F`, which
       then describes only the stable ones.

Derived properties
------------------

Everything below comes from the same :math:`\alpha^2F` and costs nothing
extra.

**Superconducting** :math:`T_\mathrm{c}`. The property :math:`\alpha^2F` was
invented to predict. Calango evaluates the Allen--Dynes form,

.. math::

   T_\mathrm{c} = f_1 f_2 \frac{\omega_{\log}}{1.2}
     \exp\!\left[\frac{-1.04(1+\lambda)}
                        {\lambda - \mu^*(1 + 0.62\lambda)}\right],

with the strong-coupling correction :math:`f_1` and the spectral-shape
correction :math:`f_2` (the latter needs the second moment
:math:`\bar\omega_2`, which is reported). McMillan's original
:math:`\theta_\mathrm{D}/1.45` form is reported beside it when a Debye
temperature is available, because the older literature quotes it --- but
:math:`\omega_{\log}` is a property of the *computed* spectrum whereas
:math:`\theta_\mathrm{D}` is a fit to a different measurement, so
Allen--Dynes is the one to use.

When :math:`\lambda \le \mu^*(1 + 0.62\lambda)` the formula has no
solution: the screened repulsion beats the phonon attraction. Calango reports
that as *"not a phonon-mediated superconductor at this coupling"* rather than
as :math:`T_\mathrm{c} = 0`, which would be indistinguishable from a
converged calculation of a very small :math:`T_\mathrm{c}`.

*How well does the closed form do?* The ``superconductivity`` unit test drives
it from **literature** :math:`\lambda` and :math:`\omega_{\log}` — not from a
Calango run — so what it measures is the formula, cleanly separated from the
:math:`\alpha^2F` that feeds it:

.. list-table::
   :header-rows: 1
   :widths: 10 12 18 15 15 15 15

   * - Metal
     - :math:`\lambda`
     - :math:`\omega_{\log}`
     - :math:`\mu^*`
     - Uncorrected
     - Corrected
     - Measured
   * - Pb
     - 1.55
     - 56 K
     - 0.10
     - 6.58 K
     - **7.52 K**
     - 7.19 K
   * - Al
     - 0.43
     - 296 K
     - 0.12
     - —
     - **1.172 K**
     - 1.18 K

Lead is the case Allen and Dynes wrote :math:`f_1` and :math:`f_2` *for*: at
:math:`\lambda = 1.55` the uncorrected McMillan-style form **undershoots** the
measurement by 8 %, and the corrections carry it past it by 5 %. Aluminium, at
weak coupling where :math:`f_1 f_2 \approx 1`, lands within a per cent — but
that per cent is an artefact of the :math:`\mu^*` chosen, which is the next
point.

.. admonition:: What :math:`\mu^*` should I use?
   :class: tip

   Not a single value. :math:`\mu^*` is the Morel--Anderson *pseudopotential*:
   the bare Coulomb repulsion :math:`\mu = N(E_\mathrm{F})\langle V_c\rangle`
   renormalized down by retardation, because the electron repulsion is
   instantaneous while the phonon attraction is slow:

   .. math::

      \mu^* = \frac{\mu}{1 + \mu \ln(E_\mathrm{F}/\omega_{ph})}.

   That logarithm is why :math:`\mu^*` lands near 0.1: :math:`\mu` is
   0.3--0.5 for a simple metal and the log is 5--7. It is therefore
   **material-dependent**, not universal --- it depends on your material's
   bandwidth-to-phonon-frequency ratio, which Calango reports as
   ``retardation_log`` beside the ``occupied_bandwidth_eV`` it was formed
   from. Multiply your own bare :math:`\mu` by
   :math:`1/(1 + \mu\cdot\texttt{retardation\_log})` to convert.

   Conventional starting points: **0.10--0.13 for sp metals** (Al, Pb, Sn,
   In), **0.12--0.15 for transition metals** (Nb, V, Ta), where d-electron
   correlation raises :math:`\mu`.

   **The trap.** Much of the literature *fits* :math:`\mu^*` to reproduce a
   measured :math:`T_\mathrm{c}`. That is legitimate calibration, but it
   means the resulting :math:`T_\mathrm{c}` is not a prediction. If you
   calibrate on a known material and apply the value to a related one, say so.

   **What Calango does about it.** Every run reports ``tc_vs_mu_star``, a
   sweep over :math:`\mu^* = 0.08`--:math:`0.16`. Quote that range. For
   aluminium it spans 2.7 K down to 0.36 K --- a factor of 7.5 --- which is
   the honest width of the prediction, not a defect.

   The real solution is to eliminate :math:`\mu^*` entirely, via
   superconducting DFT (SCDFT) or a cRPA Coulomb kernel. Neither is reachable
   through GPAW, so it is out of scope here.

**Transport:** :math:`\lambda_{tr}`, :math:`\tau_{tr}` **and**
:math:`\rho(T)`. The same sums reweighted by
:math:`1 - \cos\theta`, with :math:`\theta` the angle between band
velocities at :math:`\mathbf{k}` and :math:`\mathbf{k}+\mathbf{q}`. That
factor is the entire difference between the mass-enhancement :math:`\lambda`
and the transport one: forward scattering barely disturbs a current,
backscattering reverses it, and only the latter causes resistance.

.. note::

   :math:`\lambda_{tr}` is **not** bounded above by :math:`\lambda`. Since
   :math:`1-\cos\theta \in [0,2]`, the only general bound is
   :math:`\lambda_{tr} \le 2\lambda`. Forward scattering dominates at small
   :math:`\mathbf{q}` (where :math:`1-\cos\theta = q^2/2k_\mathrm{F}^2` for
   a free-electron sphere, so :math:`\lambda_{tr} \to 0`), and
   backscattering dominates as :math:`q \to 2k_\mathrm{F}`. A run reporting
   :math:`\lambda_{tr} > \lambda` is telling you the q-mesh is coarse
   relative to the Fermi surface, not that something is broken.

The resistivity follows from Drude written so that every quantity in it is
computed rather than assumed, :math:`\rho = 1/(\varepsilon_0\omega_p^2
\tau_{tr})`. :math:`\hbar\omega_p` comes from the Optics or K-point
Convergence module; without it :math:`\rho` is **skipped, not estimated**,
because :math:`\rho \propto 1/\omega_p^2` and a guessed :math:`\omega_p`
would produce a number that looks like a measurement.

**Mode-resolved coupling and phonon linewidths.** :math:`\lambda_{q\nu}`,
normalized so that :math:`N_q^{-1}\sum_{q\nu}\lambda_{q\nu} = \lambda`, and
the linewidth :math:`\gamma_{q\nu} = \pi N(E_\mathrm{F})
\omega_{q\nu}^2 \lambda_{q\nu}` it implies. A single :math:`\lambda` cannot
distinguish coupling concentrated in one soft branch from the same total
spread across the spectrum. The linewidth is also the one quantity here
directly measurable --- by inelastic neutron scattering --- on the same
material, rather than comparable only against a tabulated constant.

**Mass enhancement** :math:`m^*/m = 1 + \lambda`, the factor by which the
electron--phonon interaction renormalizes the band mass at the Fermi surface,
and hence the linear specific-heat coefficient :math:`\gamma`.

Validation, and what is still wrong
-----------------------------------

Two things are validated separately, and it matters which is which.

**The analysis chain is validated in closed form.** ``ElectronPhononAnalysis``
is exercised on a contrived case that has an exact answer: a free-electron
band, one phonon mode at a fixed :math:`\omega_0`, and a constant
:math:`|g|^2`. Then :math:`\lambda = 2\,g_0^2\zeta(q)/
(N(E_\mathrm{F})N_q\,\omega_0)` end to end with nothing fitted, so a wrong
normalization, a wrong band sum or a missing :math:`1/N(E_\mathrm{F})` shows
up as a factor rather than as a plausible number. The same test pins
:math:`\hbar/\tau = 2\pi\lambda k_\mathrm{B}T`, the exact factor of two in the
Drude rate, the :math:`\lambda_{tr} \le 2\lambda` bound, and the resistivity
against :math:`1/(\varepsilon_0\omega_p^2\tau_{tr})`.

**The shipped aluminium run is not.** ``al_electron_phonon_benchmark.py``
drives all three ``gpaw.elph`` stages on fcc Al and asserts
:math:`0.05 < \lambda < 3.0` — a window deliberately wide, because a
:math:`2\times2\times2` supercell on a :math:`6^3` k-mesh puts only about six
of 864 states within 0.1 eV of :math:`E_\mathrm{F}` and cannot converge
anything.

.. warning::

   That run has been observed to produce :math:`\lambda \approx 21`, against a
   literature value near 0.4 — fifty times too large, and outside the
   assertion window, so the benchmark fails on it. **The cause has not been
   identified.**

   What is known: the analysis chain above passes its closed-form test, and
   GPAW's own ``elph`` reference test passes, so the arithmetic downstream of
   the matrix elements is not the suspect. The most likely culprit is the
   *run* — specifically its :math:`2\times2\times2` supercell, which is small
   enough that aluminium comes out with **imaginary acoustic branches**, and
   :math:`\alpha^2F` integrates :math:`1/\omega`.

   Two consequences for anyone using this module for a number rather than for
   a shape:

   #. **Validate on a material whose** :math:`\lambda` **you already know**
      before trusting an absolute value from a new system. A ratio between two
      systems computed at the same settings is far more defensible than either
      value alone.
   #. **Treat a non-zero** ``excluded_modes`` **as a stop signal, not a
      footnote.** It says the structure is not at a local minimum in the
      supercell you used. The excluded branches do not merely drop out of
      :math:`\alpha^2F` — their presence means the dynamical matrix that
      produced the rest of the spectrum is describing a saddle point.

Aluminium remains the right *choice* of reference — nearly free-electron,
:math:`\lambda \approx 0.4` well established, and the room-temperature
:math:`\tau \approx 10` fs that follows from Allen's relation is within a few
femtoseconds of a Drude fit to the measured optical conductivity. It is the
particular cheap benchmark cell that is not yet giving it.

.. seealso::

   :doc:`/simulations/phonons`
      The finite-displacement machinery this module shares.

   :doc:`/electronic/optics`
      Where :math:`\tau` is consumed, as the Drude relaxation time.

   :doc:`/simulations/convergence`
      The k-point sweep can measure the plasma frequency directly — the other
      half of the Drude model.
