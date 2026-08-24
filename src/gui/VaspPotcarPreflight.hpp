#pragma once

#include <QString>
#include <QStringList>

namespace calango::gui {

/// Whether a configured VASP POTCAR directory actually resolves — a LOCAL
/// approximation of the exact check the generated script performs at
/// runtime (AseScriptGenerator.cpp, emitVasp()), run here in C++ so a
/// missing/misconfigured library is caught before anything is staged or
/// submitted, not after a remote queue wait. See PythonPackagePreflight.hpp
/// for the sibling pattern this follows.
///
/// This can only ever inspect the LOCAL filesystem. A remote job whose HPC
/// cluster profile carries its own POTCAR-directory override (HPC panel ->
/// Scheduler -> VASP POTCAR directory) points at a path on a machine this
/// process cannot see — that path is validated only when the job actually
/// runs, by the same in-script check. See
/// docs/sphinx/source/simulations/remote.md
/// and FUTURE.md ("remote POTCAR pre-flight over SSH") for that honestly
/// left gap. Checking the LOCAL configuration anyway is still worthwhile —
/// the common case is a cluster reusing the same library layout, and
/// catching "nothing configured at all" before staging beats not catching
/// it at all (the same reasoning MaceTrainerDialog::preflightMaceTorch()
/// documents for its own local-only remote approximation).
struct VaspPotcarPreflightResult {
    bool ok = false;
    /// The directory actually searched — the configured path, empty if none
    /// was configured. Never fabricated.
    QString searchedPath;
    /// Populated only when `elements` was non-empty and the directory
    /// itself resolved: element symbols with no POTCAR found under it.
    QStringList missingElements;
    /// Human-readable reason, empty when `ok` is true. Never a raw
    /// exception/traceback — always naming the specific path or elements.
    QString errorMessage;
};

/// `elements`, when supplied, restricts the check to exactly those symbols
/// (typically the unique species of the structure about to be run) — an
/// empty list checks only that the directory itself resolves to something
/// usable, which is what a caller with no structure-level element list on
/// hand should pass. Both documented layouts are recognised, exactly like
/// the in-script resolver: the configured directory as the PARENT of the
/// family dirs, and the configured directory AS the family level (element
/// folders directly inside it, which the script handles with a symlink
/// shim).
///
/// `xc` is the run's exchange-correlation functional, and it is REQUIRED to
/// get right: ASE derives the POTCAR family from it
/// (core::vaspPotcarFamilyDir — potpaw_PBE for the PBE-based functionals,
/// the unversioned potpaw for LDA). This check used to try a fixed list and
/// accept whichever family existed first, so a PBE-only library passed under
/// xc=LDA and the run then failed inside ASE looking for `potpaw`: a correct
/// directory reported as a missing POTCAR. An empty `xc` is treated as the
/// PBE family, matching what the wizard's own combo defaults to.
VaspPotcarPreflightResult checkVaspPotcar(const QString& potcarPath,
                                          const QStringList& elements = {},
                                          const QString& xc = {});

} // namespace calango::gui
