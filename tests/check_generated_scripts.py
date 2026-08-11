#!/usr/bin/env python3
"""Static checks over the Python that Calango's script generators emit.

Byte-compilation is not enough, and this file exists because of what it misses.
A generated script that USES a variable the generator never emitted the
definition of is perfectly valid Python — it compiles, it passes py_compile,
and it dies at run time with a NameError partway through somebody's
calculation. That has now happened twice. Both times the generator was edited
to add a use and the matching definition was lost.

So two checks, in order of what they catch:

  1. It compiles. Catches indentation slips in the nested blocks the
     generators assemble from string fragments, which is the failure mode a
     hand-read misses.

  2. Every generator-internal name that is READ is also WRITTEN somewhere in
     the same file. Scoped deliberately to names beginning with an underscore
     — the convention every generator uses for its own temporaries — because
     that is exactly the set the generator is responsible for and excludes
     everything supplied by an import, a builtin or the user's own script
     body. Narrow on purpose: a check with false positives gets suppressed,
     and a suppressed check catches nothing.

The name collection is deliberately FLAT: every Store anywhere in the module
counts, with no scope analysis. That under-reports (a name assigned only
inside a function would satisfy a module-level use) and never over-reports,
which is the right trade for a check that must not cry wolf.

Usage:  check_generated_scripts.py <directory-of-.py-files>
"""

from __future__ import annotations

import ast
import sys
from pathlib import Path


def assigned_names(tree: ast.AST) -> set[str]:
    """Every name the module binds, anywhere, by any means."""
    names: set[str] = set()
    for node in ast.walk(tree):
        if isinstance(node, ast.Name) and isinstance(node.ctx, ast.Store):
            names.add(node.id)
        elif isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef,
                               ast.ClassDef)):
            names.add(node.name)
            args = getattr(node, "args", None)
            if args is not None:
                for group in (args.posonlyargs, args.args, args.kwonlyargs):
                    names.update(a.arg for a in group)
                for extra in (args.vararg, args.kwarg):
                    if extra is not None:
                        names.add(extra.arg)
        elif isinstance(node, ast.Lambda):
            args = node.args
            for group in (args.posonlyargs, args.args, args.kwonlyargs):
                names.update(a.arg for a in group)
            for extra in (args.vararg, args.kwarg):
                if extra is not None:
                    names.add(extra.arg)
        elif isinstance(node, (ast.Import, ast.ImportFrom)):
            for alias in node.names:
                names.add(alias.asname or alias.name.split(".")[0])
        elif isinstance(node, ast.ExceptHandler) and node.name:
            names.add(node.name)
        elif isinstance(node, (ast.Global, ast.Nonlocal)):
            names.update(node.names)
    return names


def _guarded_reads(tree: ast.AST) -> set[int]:
    """Name nodes whose possible absence is handled on purpose.

    Reading a name that may not exist is legitimate in exactly one situation:
    the read sits in a `try` whose handler catches NameError. The generators
    use it deliberately — a block that reuses an expensive object built by an
    earlier optional stage, and rebuilds it when that stage did not run.

    Exempting those is not a loosening of the check, it is the difference
    between a check people keep and a check people switch off. Identified by
    node identity rather than by name, so a guarded read of `_x` in one place
    does not excuse an unguarded read of `_x` somewhere else in the file.
    """
    exempt: set[int] = set()
    for node in ast.walk(tree):
        if not isinstance(node, ast.Try):
            continue
        catches_name_error = False
        for handler in node.handlers:
            if handler.type is None:  # bare except
                catches_name_error = True
                break
            candidates = (handler.type.elts
                          if isinstance(handler.type, ast.Tuple)
                          else [handler.type])
            for candidate in candidates:
                if isinstance(candidate, ast.Name) and candidate.id in (
                        "NameError", "Exception", "BaseException"):
                    catches_name_error = True
        if not catches_name_error:
            continue
        for statement in node.body:
            for inner in ast.walk(statement):
                if isinstance(inner, ast.Name):
                    exempt.add(id(inner))
    return exempt


def undefined_internals(tree: ast.AST, defined: set[str]) -> list[tuple[int, str]]:
    """Reads of a generator-internal `_name` that nothing ever binds."""
    exempt = _guarded_reads(tree)
    problems: list[tuple[int, str]] = []
    seen: set[str] = set()
    for node in ast.walk(tree):
        if not isinstance(node, ast.Name) or not isinstance(node.ctx, ast.Load):
            continue
        name = node.id
        if not name.startswith("_") or name.startswith("__"):
            continue  # dunders are the interpreter's, not the generator's
        if name in defined or name in seen or id(node) in exempt:
            continue
        seen.add(name)
        problems.append((node.lineno, name))
    return sorted(problems)


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        print(f"usage: {argv[0]} <directory>", file=sys.stderr)
        return 2
    directory = Path(argv[1])
    scripts = sorted(directory.glob("*.py"))
    if not scripts:
        print(f"no generated scripts found in {directory}", file=sys.stderr)
        return 2

    failures = 0
    for path in scripts:
        source = path.read_text(encoding="utf-8")
        try:
            tree = ast.parse(source, filename=str(path))
        except SyntaxError as error:
            print(f"FAIL {path.name}: line {error.lineno}: {error.msg}")
            failures += 1
            continue
        try:
            compile(tree, str(path), "exec")
        except (SyntaxError, ValueError) as error:
            print(f"FAIL {path.name}: does not compile: {error}")
            failures += 1
            continue

        problems = undefined_internals(tree, assigned_names(tree))
        if problems:
            for line, name in problems:
                print(f"FAIL {path.name}:{line}: '{name}' is read but never "
                      f"assigned — the generator emits the use without the "
                      f"definition")
            failures += 1

    print(f"\n{len(scripts)} generated script(s) checked, {failures} failing.")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
