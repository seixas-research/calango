#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# create_conda.sh — build the Calango Conda package (.conda) from the recipe
# in this directory, ready for conda-forge.
#
# Workflow:
#   1. Find a build tool: conda-build (`conda build` / `conda-build`), boa
#      (`conda mambabuild`), or rattler-build (only with a recipe.yaml).
#   2. Validate the recipe (meta.yaml / recipe.yaml) is present.
#   3. Build with the libmamba solver + conda-forge, emitting the modern
#      `.conda` binary format into packaging/conda/dist/.
#   4. Report the artifact path and size.
#
# Usage:  packaging/conda/create_conda.sh
# Env overrides: CHANNELS (default "-c conda-forge"), OUTPUT_DIR, CONDA_SOLVER.
# ---------------------------------------------------------------------------
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RECIPE_DIR="$SCRIPT_DIR"
OUTPUT_DIR="${OUTPUT_DIR:-$SCRIPT_DIR/dist}"
CHANNELS="${CHANNELS:--c conda-forge}"
# The classic solver in conda 26.x fails this recipe on the platform virtual
# packages (__osx/__unix/__archspec/__conda); libmamba resolves it.
export CONDA_SOLVER="${CONDA_SOLVER:-libmamba}"

# --- 2. Recipe sanity check ------------------------------------------------
if [[ ! -f "$RECIPE_DIR/meta.yaml" && ! -f "$RECIPE_DIR/recipe.yaml" ]]; then
    echo "error: no meta.yaml or recipe.yaml in $RECIPE_DIR" >&2
    exit 1
fi
echo "==> Recipe: $RECIPE_DIR ($(ls "$RECIPE_DIR"/{meta,recipe}.yaml 2>/dev/null | xargs -n1 basename | paste -sd, -))"

# --- 1. Build-tool detection -----------------------------------------------
# BUILDER is an array holding the command + its fixed args.
BUILDER=()
FORMAT_FLAG=()
if [[ -f "$RECIPE_DIR/recipe.yaml" ]] && command -v rattler-build >/dev/null 2>&1; then
    BUILDER=(rattler-build build --recipe "$RECIPE_DIR/recipe.yaml")
    echo "==> Build tool: rattler-build ($(rattler-build --version 2>&1 | head -1))"
elif command -v conda >/dev/null 2>&1 && conda build --help >/dev/null 2>&1; then
    BUILDER=(conda build "$RECIPE_DIR")
    echo "==> Build tool: conda-build ($(conda build --version 2>&1 | head -1))"
    # Enforce the .conda (package format 2) output where the flag exists.
    conda build --help 2>&1 | grep -q -- '--package-format' && FORMAT_FLAG=(--package-format 2)
elif command -v conda-build >/dev/null 2>&1; then
    BUILDER=(conda-build "$RECIPE_DIR")
    echo "==> Build tool: conda-build ($(conda-build --version 2>&1 | head -1))"
    conda-build --help 2>&1 | grep -q -- '--package-format' && FORMAT_FLAG=(--package-format 2)
elif command -v conda >/dev/null 2>&1 && conda mambabuild --help >/dev/null 2>&1; then
    BUILDER=(conda mambabuild "$RECIPE_DIR")   # boa
    echo "==> Build tool: boa / conda mambabuild"
else
    echo "error: no supported build tool found. Install one of:" >&2
    echo "         conda install -n base -c conda-forge conda-build conda-libmamba-solver" >&2
    echo "         (or rattler-build / boa)" >&2
    exit 1
fi

mkdir -p "$OUTPUT_DIR"

# --- 3. Build --------------------------------------------------------------
echo "==> Building .conda into $OUTPUT_DIR (solver: $CONDA_SOLVER)"
if [[ "${BUILDER[0]}" == "rattler-build" ]]; then
    "${BUILDER[@]}" ${CHANNELS} --output-dir "$OUTPUT_DIR"
else
    # conda-build / boa
    "${BUILDER[@]}" ${CHANNELS} \
        --output-folder "$OUTPUT_DIR" \
        --no-anaconda-upload \
        "${FORMAT_FLAG[@]}"
fi

# --- 4. Report -------------------------------------------------------------
ARTIFACT="$(/usr/bin/find "$OUTPUT_DIR" -name '*.conda' -type f -print0 2>/dev/null \
    | xargs -0 ls -t 2>/dev/null | head -1 || true)"
if [[ -n "$ARTIFACT" && -f "$ARTIFACT" ]]; then
    echo ""
    echo "==> SUCCESS"
    echo "    Artifact : $ARTIFACT"
    echo "    Size     : $(du -h "$ARTIFACT" | cut -f1)"
    echo "    Format   : .conda (conda package format 2)"
else
    echo "error: no .conda artifact was produced under $OUTPUT_DIR" >&2
    exit 1
fi
