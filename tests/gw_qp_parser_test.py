#!/usr/bin/env python3
"""Yambo quasiparticle-report parser test.

The Yambo G0W0 pipeline is the longest script Calango generates and the only
one whose result depends on parsing a *third-party text format*. It cannot be
run here (Yambo is an external MPI code), and py_compile only proves it parses
— so the parser itself is exercised directly.

The functions under test are extracted BY NAME from the generated script with
``ast``, so this tests the code that actually ships rather than a copy of it.
If ``parse_qp_report`` is renamed or stops being self-contained, this fails.

Usage:  gw_qp_parser_test.py <calango_script_test binary | generated .py>

Given the script-test binary it dumps a fresh script first, so the parser under
test is always the one the current generator emits.
"""
import ast
import subprocess
import sys
import tempfile
import textwrap
from pathlib import Path

WANTED = {"qp_header_columns", "parse_qp_report"}
CONSTANTS = {"QP_ALIASES", "QP_DEFAULT_COLUMNS"}
STDLIB_IMPORTS = {"glob", "os", "re", "shutil", "subprocess", "json"}

failures = 0


def check(condition, what):
    global failures
    print(f"  {'ok  ' if condition else 'FAIL'} {what}")
    if not condition:
        failures += 1


def load_parser(script_path):
    """Compile just the parser out of the generated script.

    Only the named functions and their module-level constants are taken, so
    nothing in the pipeline (subprocess calls, GPAW imports) is executed.
    """
    tree = ast.parse(Path(script_path).read_text())
    picked = []
    for node in tree.body:
        if isinstance(node, ast.FunctionDef) and node.name in WANTED:
            picked.append(node)
        elif isinstance(node, ast.Assign) and any(
            isinstance(t, ast.Name) and t.id in CONSTANTS for t in node.targets
        ):
            picked.append(node)
        elif isinstance(node, ast.Import) and all(
            alias.name in STDLIB_IMPORTS for alias in node.names
        ):
            # The parser leans on the script's own `import re`. Carrying the
            # plain stdlib imports keeps it running as written; anything else
            # (numpy, gpaw) is deliberately left out so no pipeline code can
            # come along for the ride.
            picked.append(node)
    missing = WANTED - {n.name for n in picked if isinstance(n, ast.FunctionDef)}
    if missing:
        raise SystemExit(f"generated script defines no {sorted(missing)}")

    namespace = {}
    exec(compile(ast.Module(body=picked, type_ignores=[]), script_path, "exec"),
         namespace)
    return namespace


def write(tmp, name, text):
    path = Path(tmp) / name
    path.write_text(textwrap.dedent(text).lstrip("\n"))
    return str(path)


def resolve_script(argument, workdir):
    """Path to the generated Yambo script, dumping it first if needed."""
    if argument.endswith(".py"):
        return argument
    dump = Path(workdir) / "scripts"
    dump.mkdir()
    subprocess.run([argument, "--dump", str(dump)], check=True,
                   stdout=subprocess.DEVNULL)
    script = dump / "gw_yambo_ppa.py"
    if not script.exists():
        raise SystemExit(f"{argument} --dump produced no {script.name}")
    return str(script)


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)

    with tempfile.TemporaryDirectory() as tmp:
        ns = load_parser(resolve_script(sys.argv[1], tmp))
        parse = ns["parse_qp_report"]
        # -- The documented Yambo 5 layout: K-point before Band -------------
        print("Standard report:")
        path = write(tmp, "o-gw.qp", """
            #
            # K-point   Band     Eo [eV]   E-Eo [eV]   Sc|Eo [eV]
            #
              1         4       -0.500000   -0.250000   -1.10
              1         5        1.200000    0.400000    2.30
              2         4       -0.800000   -0.300000   -1.20
              2         5        1.500000    0.450000    2.40
        """)
        rows = parse(path)
        check(len(rows) == 4, "reads every data row")
        # (band, kpoint, e_dft, e_qp) — the header says column 0 is the
        # k-point, so band must come from column 1, not column 0.
        check(rows[0][0] == 4 and rows[0][1] == 1,
              "assigns band and k-point from the header, not by position")
        check(abs(rows[0][3] - (-0.75)) < 1e-9,
              "E_qp = Eo + (E-Eo)")
        check(abs(rows[1][3] - 1.6) < 1e-9, "correction applied per row")

        # -- The opposite column order, declared in the header --------------
        # This is the whole reason the header is parsed: the same numbers in
        # the other order must NOT silently swap band and k-point.
        print("Report with Band before K-point:")
        swapped = write(tmp, "o-swapped.qp", """
            # Band   K-point   Eo [eV]   E-Eo [eV]
              4      1        -0.500000  -0.250000
              5      1         1.200000   0.400000
        """)
        rows = parse(swapped)
        check(rows[0][0] == 4 and rows[0][1] == 1,
              "band and k-point follow the header order")
        check(abs(rows[0][2] - (-0.5)) < 1e-9, "energy column still located")

        # -- No header at all: fall back to the documented default ----------
        print("Report with no header:")
        bare = write(tmp, "o-bare.qp", """
              1   4   -0.500000   -0.250000
              2   4   -0.800000   -0.300000
        """)
        rows = parse(bare)
        check(len(rows) == 2, "parses headerless data")
        check(rows[0][1] == 1 and rows[0][0] == 4,
              "default order is K-point, Band, Eo, E-Eo")

        # -- Absolute quasiparticle energy instead of a correction ----------
        print("Report giving E rather than E-Eo:")
        absolute = write(tmp, "o-abs.qp", """
            # K-point  Band  Eo [eV]  E [eV]
              1        4    -0.500000  -0.750000
        """)
        rows = parse(absolute)
        check(len(rows) == 1 and abs(rows[0][3] - (-0.75)) < 1e-9,
              "uses E directly when no correction column exists")

        # -- Junk must be skipped, not guessed at ---------------------------
        print("Malformed content:")
        junk = write(tmp, "o-junk.qp", """
            # K-point  Band  Eo [eV]  E-Eo [eV]
              1        4    -0.500000  -0.250000
              this is not data
              2        *    -0.800000  -0.300000

              3        4     0.100000   0.050000
        """)
        rows = parse(junk)
        check(len(rows) == 2,
              "non-numeric rows are dropped rather than half-parsed")
        check([r[1] for r in rows] == [1, 3], "the surviving rows are the valid ones")

        empty = write(tmp, "o-empty.qp", "# K-point  Band  Eo [eV]  E-Eo [eV]\n")
        check(parse(empty) == [],
              "a report with no data yields no rows (the caller raises)")

    print("\nAll parser checks passed." if failures == 0
          else f"\n{failures} check(s) FAILED.")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
