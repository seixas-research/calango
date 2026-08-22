#!/usr/bin/env python3
"""Fail the Linux packaging build if a shipped ELF needs a library the .deb
does not declare.

Written after calango 26.8.36 shipped a .deb that dynamically needed
libmkl_intel_lp64.so.3 / libmkl_intel_thread.so.3 / libmkl_core.so.3 /
libiomp5.so -- present on the build machine (an unconstrained
find_package(LAPACK) picked up MKL there), completely absent from the
control file's Depends, and therefore absent on every clean machine
`apt install` was run on. Not even a Calango-specific gap: CMake's own
CPackDeb.cmake unconditionally passes `dpkg-shlibdeps --ignore-missing-info`
when the installed dpkg-shlibdeps supports it, which downgrades what would
otherwise be a hard "no dependency information found" build failure into a
silent omission of exactly that dependency -- true for every project with
CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON, not just this one. The fix to the root
cause was pinning BLA_VENDOR via a since-removed CALANGO_BLAS option --
superseded by removing the native DFT/DFTB engines that were LAPACK's only
consumers entirely (see CMakeLists.txt). This script is the backstop that
turns the NEXT such leak, on any other library, into a build failure here,
not a bug report from a user's machine.

Usage (run from the Linux build machine, after `cpack` produced the .deb,
before it is published -- packaging/linux/build_deb.sh does this):

    python3 packaging/linux/audit_elf_deps.py path/to/calango_*.deb

Exit status: 0 if every NEEDED entry in every shipped ELF is satisfied by
the base-system allowlist, a library bundled in the same package, or a
package actually named in the .deb's own Depends/Pre-Depends -- nonzero
(with every offending soname listed) otherwise.

Two resolution modes:
  * dpkg mode (this machine has `dpkg`, i.e. this IS the Debian/Ubuntu
    packaging machine): authoritative -- `dpkg -S` on the resolved file's
    real path says which package, if any, owns it.
  * heuristic mode (no `dpkg` -- e.g. exercising this script on a
    developer's non-Debian machine): a small soname-prefix -> package-name
    substring table stands in. Less precise (can't catch a version skew),
    but still catches the actual failure class: a soname matching no known
    family and no declared Depends. Intel MKL and the Intel OpenMP runtime
    are hard-banned in BOTH modes regardless of what Depends says, since a
    packaged calango must never need them at all (see CMakeLists.txt).
"""

from __future__ import annotations

import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
from pathlib import Path


def _safe_extractall(archive: Path, dest: Path) -> None:
    """tarfile's `filter=` kwarg (PEP 706) only exists on Python >= 3.12;
    older interpreters (this script's only hard requirement is whatever
    `python3` build_deb.sh's own machine has) don't accept it at all."""
    with tarfile.open(archive) as tf:
        try:
            tf.extractall(dest, filter="data")
        except TypeError:
            tf.extractall(dest)

# Always present on a bootstrapped Debian/Ubuntu system (glibc + gcc
# runtime + the dynamic linker) -- guaranteed without being named in
# Depends, exactly like Qt/HDF5/Python are guaranteed only THROUGH Depends.
BASE_SYSTEM_ALLOWLIST = {
    "libc.so.6", "libm.so.6", "libpthread.so.0", "libdl.so.2", "librt.so.1",
    "libgcc_s.so.1", "libstdc++.so.6", "libutil.so.1", "libresolv.so.2",
    "libnsl.so.1", "libanl.so.1",
    "ld-linux-x86-64.so.2", "ld-linux.so.2", "ld-linux-aarch64.so.1",
    "ld-linux-armhf.so.3",
}

# Never acceptable in a shipped binary, no matter what Depends says --
# these are exactly the sonames the 26.8.36 incident shipped, back when
# the native DFT/DFTB engines still linked LAPACK at all (see
# CMakeLists.txt; both were since removed, taking every BLAS dependency
# with them). Banned outright rather than relying on that removal alone:
# a build whose artifact still needs these at runtime must never leave
# this machine, whatever introduces the link.
BANNED_SONAME_PATTERNS = [
    re.compile(r"^libmkl_"),
    re.compile(r"^libiomp5"),
    re.compile(r"^libimf\."),
    re.compile(r"^libirc\."),
    re.compile(r"^libsvml\."),
    re.compile(r"^libintlc\."),
]

# Heuristic-mode fallback only: soname prefix -> substring that must appear
# in some declared Depends/Pre-Depends package name. Extend this table when
# a new legitimately-linked family is added; a soname matching nothing here
# AND unresolved by dpkg is a build failure by design, not a false alarm to
# silence -- update the table (or add the missing Depends) instead.
FAMILY_PATTERNS = [
    (re.compile(r"^libQt6"), "qt6"),
    (re.compile(r"^libpython3"), "python3"),
    (re.compile(r"^libhdf5"), "hdf5"),
    (re.compile(r"^lib(open)?blas"), ("blas", "lapack")),
    (re.compile(r"^liblapack"), ("blas", "lapack")),
    (re.compile(r"^libGL"), ("libgl", "mesa")),
    (re.compile(r"^libEGL"), ("libegl", "mesa")),
]


def die(msg: str) -> "NoReturn":
    print(f"error: {msg}", file=sys.stderr)
    sys.exit(1)


def require_tool(name: str) -> None:
    if shutil.which(name) is None:
        die(f"'{name}' not found on PATH -- this script must run on the "
            f"Linux packaging machine (see packaging/linux/build_deb.sh)")


def parse_depends(control_text: str) -> set[str]:
    """Flatten Depends/Pre-Depends fields into a set of bare package names
    (version constraints and alternatives stripped)."""
    names: set[str] = set()
    for field in ("Depends", "Pre-Depends"):
        m = re.search(rf"^{field}:\s*(.+(?:\n .+)*)", control_text, re.MULTILINE)
        if not m:
            continue
        raw = m.group(1).replace("\n", " ")
        for alt_group in raw.split(","):
            for alt in alt_group.split("|"):
                name = alt.strip().split(" ")[0].strip()
                if name:
                    names.add(name)
    return names


def is_elf(path: Path) -> bool:
    try:
        with path.open("rb") as fh:
            return fh.read(4) == b"\x7fELF"
    except OSError:
        return False


def needed_and_rpath(objdump: str, path: Path) -> tuple[list[str], list[str]]:
    out = subprocess.run([objdump, "-p", str(path)], capture_output=True,
                          text=True, check=False).stdout
    needed, rpaths = [], []
    for line in out.splitlines():
        line = line.strip()
        if line.startswith("NEEDED"):
            needed.append(line.split()[1])
        elif line.startswith("RPATH") or line.startswith("RUNPATH"):
            rpaths.append(line.split(None, 1)[1])
    return needed, rpaths


def dpkg_owner(path: str) -> str | None:
    result = subprocess.run(["dpkg", "-S", path], capture_output=True,
                             text=True, check=False)
    if result.returncode != 0 or not result.stdout.strip():
        return None
    # "package: /path" or "package1, package2: /path"
    return result.stdout.split(":", 1)[0].strip()


def resolve_via_ldconfig(soname: str) -> str | None:
    cache = subprocess.run(["ldconfig", "-p"], capture_output=True, text=True,
                            check=False).stdout
    for line in cache.splitlines():
        if soname in line and "=>" in line:
            return line.split("=>", 1)[1].strip()
    return None


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        die("usage: audit_elf_deps.py path/to/calango_<ver>_<arch>.deb")
    deb_path = Path(argv[1])
    if not deb_path.is_file():
        die(f"{deb_path}: no such file")

    for tool in ("ar", "objdump", "tar"):
        require_tool(tool)
    objdump = "objdump"

    has_dpkg = shutil.which("dpkg") is not None
    has_ldconfig = shutil.which("ldconfig") is not None
    if not has_dpkg:
        print("warning: 'dpkg' not found -- running in heuristic mode "
              "(family-pattern matching, not authoritative package "
              "ownership). Run this on the actual Linux packaging machine "
              "for a real gate.", file=sys.stderr)

    with tempfile.TemporaryDirectory(prefix="calango-audit-") as tmp:
        tmp_path = Path(tmp)
        subprocess.run(["ar", "x", str(deb_path.resolve())], cwd=tmp_path,
                        check=True)

        control_archive = next(tmp_path.glob("control.tar*"))
        control_dir = tmp_path / "control"
        control_dir.mkdir()
        _safe_extractall(control_archive, control_dir)
        control_text = (control_dir / "control").read_text()
        declared = parse_depends(control_text)

        data_archive = next(tmp_path.glob("data.tar*"))
        data_dir = tmp_path / "data"
        data_dir.mkdir()
        _safe_extractall(data_archive, data_dir)

        elf_files = [p for p in data_dir.rglob("*") if p.is_file() and is_elf(p)]
        bundled_sonames = {p.name for p in elf_files}

        failures: list[str] = []
        checked_needed = 0

        for elf in elf_files:
            rel = elf.relative_to(data_dir)
            needed, rpaths = needed_and_rpath(objdump, elf)

            for rp in rpaths:
                for entry in rp.split(":"):
                    if entry in ("$ORIGIN", "") or entry.startswith("$ORIGIN/"):
                        continue
                    failures.append(
                        f"{rel}: RPATH/RUNPATH entry '{entry}' is not "
                        f"$ORIGIN-relative -- points at a build-machine path")

            for soname in needed:
                checked_needed += 1

                if any(p.match(soname) for p in BANNED_SONAME_PATTERNS):
                    failures.append(
                        f"{rel}: NEEDED '{soname}' is on the banned list "
                        f"(MKL / Intel OpenMP / Intel compiler runtime) -- "
                        f"a packaged build must not link this at all. The "
                        f"native DFT/DFTB engines that were LAPACK's only "
                        f"consumers are gone (see CMakeLists.txt's top-of-"
                        f"file comment), so nothing in a correctly-built "
                        f"artifact should reference these sonames any more; "
                        f"a hit here means something new started linking "
                        f"LAPACK/BLAS, or this artifact predates that "
                        f"removal")
                    continue

                if soname in BASE_SYSTEM_ALLOWLIST:
                    continue
                if soname in bundled_sonames:
                    continue

                if has_dpkg:
                    resolved = None
                    if has_ldconfig:
                        resolved = resolve_via_ldconfig(soname)
                    owner = dpkg_owner(resolved) if resolved else None
                    if owner is None:
                        failures.append(
                            f"{rel}: NEEDED '{soname}' does not resolve to "
                            f"any installed package on this machine "
                            f"(unresolvable or not dpkg-owned)")
                    elif owner not in declared:
                        failures.append(
                            f"{rel}: NEEDED '{soname}' is owned by package "
                            f"'{owner}', which is not in the .deb's own "
                            f"Depends/Pre-Depends ({sorted(declared)})")
                else:
                    matched = False
                    for pattern, substr in FAMILY_PATTERNS:
                        if pattern.match(soname):
                            matched = True
                            needles = (substr,) if isinstance(substr, str) else substr
                            if not any(any(n in dep.lower() for n in needles)
                                       for dep in declared):
                                failures.append(
                                    f"{rel}: NEEDED '{soname}' matches the "
                                    f"'{pattern.pattern}' family but no "
                                    f"declared Depends mentions "
                                    f"{needles} ({sorted(declared)})")
                            break
                    if not matched:
                        failures.append(
                            f"{rel}: NEEDED '{soname}' matches no known "
                            f"family and dpkg is unavailable to check "
                            f"ownership directly -- review by hand, then "
                            f"extend FAMILY_PATTERNS or BASE_SYSTEM_ALLOWLIST")

        print(f"Audited {len(elf_files)} ELF file(s), {checked_needed} "
              f"NEEDED entries, against Depends={sorted(declared)}")

        if failures:
            print(f"\n{len(failures)} problem(s) found:\n", file=sys.stderr)
            for f in failures:
                print(f"  - {f}", file=sys.stderr)
            return 1

        print("OK -- every NEEDED entry is satisfied by the base system, "
              "a bundled library, or a declared Depends.")
        return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
