#!/usr/bin/env python3
"""Fail the macOS packaging build if a shipped Mach-O still references a
build-machine path, OR references a bundle-relative path that does not
actually resolve to a file inside the bundle.

Companion to packaging/linux/audit_elf_deps.py. A build-machine library can
leak into a .app at either end of the load chain, and this checks both:

  * a binary installed by a plain install(TARGETS) keeps whatever absolute
    load commands it was linked with -- nothing rewrites them the way
    macdeployqt rewrites `calango` itself;
  * a library macdeployqt DID rewrite can still keep the build machine's
    original absolute LC_RPATH entries. On that machine dyld's chained
    rpath search resolves a dependency through one of them, so the bundle
    looks complete while the file was never copied into it; on a clean Mac
    every one of those paths misses and dyld aborts before main() runs.

Hence the rule this script enforces: "@rpath/..." alone is NOT proof of
relocatability, only proof of FORM. Every relative dependency is checked
for RESOLUTION against the bundle's own pooled rpath set, and every
LC_RPATH entry for relocatability.

Usage (run after the .dmg/.app is built, before publishing -- see
packaging/macos/build_dmg.sh):

    python3 packaging/macos/audit_macho_deps.py path/to/calango.app
    python3 packaging/macos/audit_macho_deps.py path/to/calango_*.dmg

Two independent checks, both must pass:

1. LC_RPATH entries. Acceptable: "@loader_path" (optionally with a
   trailing "/..." suffix) or "@executable_path" (ditto). Anything else --
   an absolute filesystem path (Homebrew, conda, $HOME, /usr/local, ...),
   or a bare relative path -- fails outright, whether or not it currently
   "resolves" on this machine: a path a clean Mac does not have is a bug
   even when it happens not to be exercised today.

2. LC_LOAD_DYLIB-family entries (excluding each file's own LC_ID_DYLIB,
   which delocate/macdeployqt-style vendoring routinely leaves pointing at
   a placeholder or build path -- cosmetic once nothing references it by
   that name). A dependency under /System/ or /usr/lib/ is always fine (part
   of the OS). A dependency prefixed @executable_path/, @loader_path/ or
   @rpath/ must actually RESOLVE to a real file inside the bundle: this
   script computes, for every Mach-O in the bundle, the set of directories
   its LC_RPATH entries expand to (@loader_path -> that file's own
   directory, @executable_path -> the main executable's directory) and
   pools them across the WHOLE bundle -- a deliberate over-approximation
   of dyld's real per-load-chain rpath search (which would use only the
   rpaths of images actually in that specific load chain). This can
   accept a dependency dyld's real chain-specific search would reject; it
   cannot miss one that is genuinely absent from the bundle everywhere,
   which is the failure mode this script exists to catch. Anything else
   (an absolute non-system path) fails as before.
"""

from __future__ import annotations

import argparse
import fnmatch
import plistlib
import re
import subprocess
import sys
from pathlib import Path

MACHO_MAGIC = {
    b"\xfe\xed\xfa\xce", b"\xce\xfa\xed\xfe",  # 32-bit
    b"\xfe\xed\xfa\xcf", b"\xcf\xfa\xed\xfe",  # 64-bit
    b"\xca\xfe\xba\xbe", b"\xbe\xba\xfe\xca",  # fat/universal
}

LOAD_CMDS = {"LC_LOAD_DYLIB", "LC_LOAD_WEAK_DYLIB", "LC_REEXPORT_DYLIB",
             "LC_LAZY_LOAD_DYLIB"}

SYSTEM_PREFIXES = ("/System/", "/usr/lib/")
RELATIVE_PREFIXES = ("@executable_path", "@loader_path", "@rpath")


def die(msg: str) -> "NoReturn":
    print(f"error: {msg}", file=sys.stderr)
    sys.exit(1)


def is_macho(path: Path) -> bool:
    try:
        with path.open("rb") as fh:
            return fh.read(4) in MACHO_MAGIC
    except OSError:
        return False


def _load_command_names(path: Path, wanted_cmds: set[str]) -> list[str]:
    """Generic otool -l parser: returns every 'name'/'path' value under a
    load command whose cmd is in wanted_cmds. Used for both LC_LOAD_DYLIB
    (name) and LC_RPATH (path) -- otool prints the same "key value (offset
    N)" shape for both."""
    out = subprocess.run(["otool", "-l", str(path)], capture_output=True,
                          text=True, check=False).stdout
    values = []
    lines = out.splitlines()
    i = 0
    while i < len(lines):
        line = lines[i].strip()
        if line.startswith("cmd ") and line.split()[1] in wanted_cmds:
            j = i + 1
            while j < len(lines) and not lines[j].strip().startswith("Load command"):
                m = re.match(r"(?:name|path) (.*) \(offset", lines[j].strip())
                if m:
                    values.append(m.group(1))
                    break
                j += 1
        i += 1
    return values


def real_needed(path: Path) -> list[str]:
    """LC_LOAD_DYLIB-family entries only -- excludes the file's own
    LC_ID_DYLIB, which `otool -L`'s first data line conflates with a real
    dependency and which the delocate/macdeployqt-vendored libraries in
    this bundle routinely leave pointing at a placeholder or build path."""
    return _load_command_names(path, LOAD_CMDS)


def real_rpaths(path: Path) -> list[str]:
    """This file's own LC_RPATH search-path entries."""
    return _load_command_names(path, {"LC_RPATH"})


def main_executable(app_path: Path) -> Path:
    plist_path = app_path / "Contents" / "Info.plist"
    if plist_path.is_file():
        try:
            with plist_path.open("rb") as fh:
                info = plistlib.load(fh)
            name = info.get("CFBundleExecutable")
            if name:
                candidate = app_path / "Contents" / "MacOS" / name
                if candidate.is_file():
                    return candidate
        except Exception:
            pass
    macos_dir = app_path / "Contents" / "MacOS"
    candidates = [p for p in macos_dir.iterdir() if p.is_file()] if macos_dir.is_dir() else []
    if not candidates:
        die(f"no executable found under {macos_dir}")
    return candidates[0]


def mount_dmg(dmg: Path) -> tuple[Path, str]:
    out = subprocess.run(
        ["hdiutil", "attach", "-nobrowse", "-readonly", str(dmg)],
        capture_output=True, text=True, check=True).stdout
    m = re.search(r"(/Volumes/[^\t\n]+?)\s*$", out.strip().splitlines()[-1])
    if not m:
        die(f"could not parse hdiutil attach output:\n{out}")
    volume = m.group(1)
    apps = list(Path(volume).glob("*.app"))
    if not apps:
        die(f"no .app bundle found on mounted volume {volume}")
    return apps[0], volume


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("target", help="path to calango.app or calango_*.dmg")
    parser.add_argument(
        "--exclude", action="append", default=[], metavar="GLOB",
        help="fnmatch-style glob (relative to the .app root, e.g. "
             "'Contents/Resources/python/*') to skip entirely -- may be "
             "given multiple times. Use sparingly and only for a payload "
             "with its own, separately-tracked relocatability contract; "
             "every exclusion narrows what this script actually verifies.")
    args = parser.parse_args(argv[1:])

    target = Path(args.target)
    if not target.exists():
        die(f"{target}: no such file or directory")

    mounted_volume = None
    if target.suffix == ".dmg":
        app_path, mounted_volume = mount_dmg(target)
    else:
        app_path = target

    try:
        exe_dir = main_executable(app_path).parent

        macho_files = [p for p in app_path.rglob("*")
                       if p.is_file() and not p.is_symlink() and is_macho(p)]

        excluded = 0
        if args.exclude:
            kept = []
            for p in macho_files:
                rel_str = str(p.relative_to(app_path))
                if any(fnmatch.fnmatch(rel_str, pat) for pat in args.exclude):
                    excluded += 1
                else:
                    kept.append(p)
            macho_files = kept
            print(f"Excluded {excluded} file(s) matching {args.exclude} "
                  f"from the audit")

        failures: list[str] = []
        checked_deps = 0
        checked_rpaths = 0

        # Pass 1: every file's own RPATH entries, checked for form AND
        # pooled (relocatable ones only) into a bundle-wide search-root set
        # used to resolve @rpath/ dependencies in pass 2.
        search_roots: set[Path] = set()
        file_rpaths: dict[Path, list[str]] = {}
        for f in macho_files:
            rel = f.relative_to(app_path)
            rpaths = real_rpaths(f)
            file_rpaths[f] = rpaths
            for rp in rpaths:
                checked_rpaths += 1
                if rp == "@loader_path" or rp.startswith("@loader_path/"):
                    base = f.parent / rp[len("@loader_path"):].lstrip("/")
                elif rp == "@executable_path" or rp.startswith("@executable_path/"):
                    base = exe_dir / rp[len("@executable_path"):].lstrip("/")
                else:
                    failures.append(
                        f"{rel}: LC_RPATH entry '{rp}' is not "
                        f"@loader_path/@executable_path-relative -- a "
                        f"build-machine path baked into the search list, "
                        f"whether or not it happens to resolve anything "
                        f"today")
                    continue
                search_roots.add(base.resolve())

        # Pass 2: every dependency, checked for acceptable form, then for
        # relative ones, checked for actual resolution against the pooled
        # search roots (a deliberate over-approximation -- see docstring).
        for f in macho_files:
            rel = f.relative_to(app_path)
            for dep in real_needed(f):
                checked_deps += 1

                if dep.startswith(SYSTEM_PREFIXES):
                    continue

                if dep.startswith("@executable_path/"):
                    candidate = exe_dir / dep[len("@executable_path/"):]
                    if not candidate.exists():
                        failures.append(
                            f"{rel}: loads '{dep}' but "
                            f"{candidate.relative_to(app_path)} does not "
                            f"exist in the bundle")
                    continue

                if dep.startswith("@loader_path/"):
                    candidate = f.parent / dep[len("@loader_path/"):]
                    if not candidate.exists():
                        failures.append(
                            f"{rel}: loads '{dep}' but it does not exist "
                            f"relative to this file's own directory")
                    continue

                if dep.startswith("@rpath/"):
                    subpath = dep[len("@rpath/"):]
                    if not any((root / subpath).exists() for root in search_roots):
                        failures.append(
                            f"{rel}: loads '{dep}' but no LC_RPATH in the "
                            f"bundle resolves it to a file that actually "
                            f"exists -- the bundle is incomplete and this "
                            f"will abort at launch on a clean Mac")
                    continue

                # Absolute or otherwise-unrecognized path outside the OS.
                failures.append(f"{rel}: loads '{dep}' -- a build-machine "
                                 f"path, not present on a clean Mac")

        print(f"Audited {len(macho_files)} Mach-O file(s): {checked_rpaths} "
              f"LC_RPATH entries, {checked_deps} LC_LOAD_DYLIB-family "
              f"entries, against {len(search_roots)} pooled search "
              f"root(s)")

        if failures:
            print(f"\n{len(failures)} problem(s) found:\n", file=sys.stderr)
            for msg in failures:
                print(f"  - {msg}", file=sys.stderr)
            return 1

        print("OK -- every RPATH is bundle-relative and every load-time "
              "dependency resolves inside the bundle or under /System or "
              "/usr/lib.")
        return 0
    finally:
        if mounted_volume:
            subprocess.run(["hdiutil", "detach", mounted_volume],
                            capture_output=True, check=False)


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
