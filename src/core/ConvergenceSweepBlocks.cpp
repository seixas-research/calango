#include "core/ConvergenceSweepBlocks.hpp"

#include <sstream>

namespace calango::core::convergence_sweep {

std::string measurementBlock(std::string_view pointLabel,
                             std::string_view eigenvalueComment)
{
    std::ostringstream out;
    out << "        energy = float(atoms.get_potential_energy())\n"
           "        forces = np.asarray(atoms.get_forces(), dtype=float)\n"
           "        # The per-atom force NORM, not the largest Cartesian\n"
           "        # component — componentwise maxima under-report by up to\n"
           "        # sqrt(3) depending on the force's direction.\n"
           "        magnitudes = (np.linalg.norm(forces, axis=1)\n"
           "                      if forces.size else np.zeros(0))\n"
           "        fmax = float(magnitudes.max()) if magnitudes.size else 0.0\n"
           "        record[\"energy_eV\"] = energy\n"
           "        record[\"energy_per_atom_eV\"] = energy / natoms\n"
           "        record[\"fmax_eV_per_A\"] = fmax\n"
        << eigenvalueComment
        << "        band_means = None\n"
           "        try:\n"
           "            calc = atoms.calc\n"
           "            nspins = calc.get_number_of_spins()\n"
           "            nkpts = len(calc.get_ibz_k_points())\n"
           "            eig = np.array(\n"
           "                [[calc.get_eigenvalues(kpt=k, spin=s)\n"
           "                  for k in range(nkpts)]\n"
           "                 for s in range(nspins)])\n"
           "            band_means = eig.mean(axis=(0, 1))\n"
           "        except Exception as eig_error:\n"
           "            # Forces and energies still stand; the third plot\n"
           "            # simply has one point fewer.\n"
           "            print(f\"CALANGO_WARN " << pointLabel
        << " no eigenvalues: \"\n"
           "                  f\"{eig_error}\", flush=True)\n"
           "        evaluated.append({\"record\": record, \"forces\": forces,\n"
           "                          \"band_means\": band_means})\n"
           "        _calango_metric(index, energy=energy, max_force=fmax)\n"
           "        print(f\"CALANGO_MEMBER " << pointLabel
        << " E={energy:.6f} \"\n"
           "              f\"fmax={fmax:.6f}\", flush=True)\n";
    return out.str();
}

std::string analysisBlock(std::string_view referenceComment,
                          std::string_view failNoun)
{
    std::ostringstream out;
    out << "# -- Convergence relative to the best run --------------------------\n"
           "if not evaluated:\n"
           "    raise RuntimeError(\"Every " << failNoun
        << " failed — nothing to use as \"\n"
           "                       \"the convergence reference.\")\n"
        << referenceComment
        << "ref = evaluated[-1]\n"
           "reference = ref[\"record\"]\n"
           "for entry in evaluated:\n"
           "    p = entry[\"record\"]\n"
           "    # ΔE = (E_total − E_reference) / N — energy convergence is\n"
           "    # judged per atom so the criterion transfers between cells.\n"
           "    p[\"delta_energy_per_atom_eV\"] = (\n"
           "        p[\"energy_eV\"] - reference[\"energy_eV\"]) / natoms\n"
           "    # Force error: the largest atom-wise |F_i − F_i,ref| — the\n"
           "    # forces are compared vector-by-vector against the reference\n"
           "    # run and NOT divided by the atom count (a force is already a\n"
           "    # per-atom quantity). Comparing max|F| scalars instead would\n"
           "    # miss two wrong forces that happen to share a magnitude.\n"
           "    p[\"force_error_eV_per_A\"] = float(\n"
           "        np.linalg.norm(entry[\"forces\"] - ref[\"forces\"],\n"
           "                       axis=1).max()) if len(atoms) else 0.0\n"
           "    p[\"delta_fmax_eV_per_A\"] = (\n"
           "        p[\"fmax_eV_per_A\"] - reference[\"fmax_eV_per_A\"])\n"
           "    # Eigenvalue convergence: mean absolute difference of the\n"
           "    # k-averaged band energies against the reference, over the\n"
           "    # bands both runs computed.\n"
           "    if (entry[\"band_means\"] is not None\n"
           "            and ref[\"band_means\"] is not None):\n"
           "        nbands = min(len(entry[\"band_means\"]),\n"
           "                     len(ref[\"band_means\"]))\n"
           "        p[\"eigenvalue_mad_eV\"] = float(\n"
           "            np.abs(entry[\"band_means\"][:nbands]\n"
           "                   - ref[\"band_means\"][:nbands]).mean())\n"
           "\n"
           "converged = [e[\"record\"] for e in evaluated]\n";
    return out.str();
}

} // namespace calango::core::convergence_sweep
