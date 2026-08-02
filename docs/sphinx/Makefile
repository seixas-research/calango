# Minimal makefile for Sphinx documentation
#

# You can set these variables from the command line, and also
# from the environment for the first two.
#
# SPHINXBUILD prefers the docs' own virtualenv when it exists (create it with
#   python3 -m venv .venv && .venv/bin/pip install -r requirements.txt
# from this directory). A bare `sphinx-build` on PATH is often Homebrew's
# sphinx-doc, whose sealed keg environment cannot see pip-installed
# extensions (myst_parser, furo, …) — the venv sidesteps that entirely.
SPHINXOPTS    ?=
SPHINXBUILD   ?= $(shell [ -x .venv/bin/sphinx-build ] && echo .venv/bin/sphinx-build || echo sphinx-build)
SOURCEDIR     = source
BUILDDIR      = build

# Put it first so that "make" without argument is like "make help".
help:
	@$(SPHINXBUILD) -M help "$(SOURCEDIR)" "$(BUILDDIR)" $(SPHINXOPTS) $(O)

.PHONY: help Makefile

# Catch-all target: route all unknown targets to Sphinx using the new
# "make mode" option.  $(O) is meant as a shortcut for $(SPHINXOPTS).
%: Makefile
	@$(SPHINXBUILD) -M $@ "$(SOURCEDIR)" "$(BUILDDIR)" $(SPHINXOPTS) $(O)
