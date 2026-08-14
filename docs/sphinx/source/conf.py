# Configuration file for the Sphinx documentation builder.
#
# Calango is a C++/Qt desktop application — there is no Python package to
# import, so this configuration carries no autodoc machinery: MyST Markdown
# pages, math, copy buttons, design components, and the Furo theme.

# -- Project information -----------------------------------------------------

project = 'Calango'
author = 'Leandro Seixas Rocha'
copyright = 'Leandro Seixas Rocha, 2026'
# Kept in step with project(calango VERSION ...) in the top-level
# CMakeLists.txt by hand — this is the only version string in the repository
# that nothing generates. `ctest -L docs` (docs_version_sync) fails when the
# two drift, which is how this was caught reading 26.8.2 against a 26.8.20
# binary: a truncation nobody would notice by eye.
release = '26.8.20'

# -- General configuration ---------------------------------------------------

extensions = [
    'sphinx.ext.mathjax',   # LaTeX equations
    'myst_parser',          # Markdown (.md) pages
    'sphinx_copybutton',    # copy button on code blocks
    'sphinx_design',        # grids, cards, tabs
]

# MyST: $...$ / $$...$$ math, LaTeX align/equation environments, and
# ::: fences for admonitions. Heading anchors allow deep links to ### level.
myst_enable_extensions = [
    'dollarmath',
    'amsmath',
    'colon_fence',
]
myst_heading_anchors = 3

templates_path = ['_templates']
exclude_patterns = []

# Numbered figures so screenshots can be referenced as "Fig. N".
numfig = True

# -- Options for HTML output -------------------------------------------------

html_title = 'Calango'

html_theme = 'furo'
html_static_path = ['_static']
html_css_files = ['custom.css']

html_theme_options = {
    'light_logo': 'icon.png',  # file in docs/sphinx/source/_static/
    'dark_logo': 'icon.png',
}

html_favicon = '_static/favicon.png'
html_show_sphinx = False
html_show_copyright = True
html_show_sourcelink = False
