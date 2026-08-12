Visual Gallery
==============

Screenshots and renders from across the application. Every figure below is a
placeholder awaiting its capture — the layout and captions are final, the
images are not.

.. tip::

   Captures should be taken at 2× scale (HiDPI) on the default dark theme,
   window width ≥ 1600 px, and saved as PNG into
   ``docs/sphinx/source/_static/img/``. File names below are already wired.

The workspace
-------------

.. figure:: _static/img/gallery_workspace_overview.png
   :alt: Default two-column workspace with a crystal loaded in the viewport
   :width: 95%
   :figclass: screenshot

   The default layout — Structure, Volumetric Data, and Processes on the left;
   Representation, Cell & Axes, and Visual Effects on the right; the Results
   dock and HPC panel along the bottom.

.. figure:: _static/img/gallery_workspace_wizard.png
   :alt: A staged simulation wizard with the live ASE script preview
   :width: 95%
   :figclass: screenshot

   A four-stage simulation wizard: every GUI choice is synthesized into the
   editable ASE script on the review page.

Modeling
--------

.. figure:: _static/img/gallery_builder_slab.png
   :alt: The three-stage surface slab wizard with the interactive lattice canvas
   :width: 95%
   :figclass: screenshot

   The surface slab wizard — dragging the in-plane vectors on the lattice
   canvas recomputes the nearest integer Miller indices.

.. figure:: _static/img/gallery_builder_polycrystal.png
   :alt: A Voronoi polycrystal colored by grain index
   :width: 95%
   :figclass: screenshot

   A Voronoi polycrystal; each atom carries its grain index as a
   color-mappable scalar field.

.. figure:: _static/img/gallery_builder_dislocation.png
   :alt: An edge dislocation core in a periodic crystal
   :width: 95%
   :figclass: screenshot

   An edge dislocation inserted by the closed-form Volterra displacement
   field.

.. figure:: _static/img/gallery_builder_interface.png
   :alt: A liquid water film packed on a slab by the interface builder
   :width: 95%
   :figclass: screenshot

   The liquid/gas interface builder — an ionic solution packed to a target
   density above a fixed surface.

Rendering
---------

.. figure:: _static/img/gallery_render_representations.png
   :alt: The same structure in ball-and-stick, space-filling, and wireframe
   :width: 95%
   :figclass: screenshot

   One structure, three representations — ball-and-stick, space-filling
   (CPK), and wireframe.

.. figure:: _static/img/gallery_render_gcn.png
   :alt: A nanoparticle colored by generalized coordination number
   :width: 95%
   :figclass: screenshot

   Generalized coordination numbers on a Wulff nanoparticle — terraces,
   edges, and vertices resolved by a continuous colormap.

.. figure:: _static/img/gallery_render_effects.png
   :alt: Depth of field and ambient occlusion on a large structure
   :width: 95%
   :figclass: screenshot

   Screen-space ambient occlusion, depth of field, and the three-light studio
   default.

.. figure:: _static/img/gallery_render_raytraced.png
   :alt: A POV-Ray render exported from the live viewport
   :width: 95%
   :figclass: screenshot

   A ray-traced export — the POV-Ray scene reproduces the viewport's camera,
   lights, and styling.

Electronic structure
--------------------

.. figure:: _static/img/gallery_electronic_bands.png
   :alt: Band structure and PDOS side by side with irrep labels
   :width: 95%
   :figclass: screenshot

   Bands beside the projected density of states — with irreducible-
   representation labels at the high-symmetry points.

.. figure:: _static/img/gallery_electronic_fatbands.png
   :alt: Orbital-projected fatbands with overlapping channels
   :width: 95%
   :figclass: screenshot

   Fatbands — per-band, per-k orbital weights drawn as thickness and color on
   one shared normalization.

.. figure:: _static/img/gallery_electronic_optics.png
   :alt: The optics viewer showing the dielectric function of silicon
   :width: 95%
   :figclass: screenshot

   The frequency-dependent dielectric function with tetrahedron-integrated
   van Hove features.

.. figure:: _static/img/gallery_electronic_bz.png
   :alt: The Brillouin zone viewer with a k-path drawn between labels
   :width: 95%
   :figclass: screenshot

   The Brillouin-zone viewer — Wigner–Seitz cell, high-symmetry labels, and a
   click-built k-path.

Analysis
--------

.. figure:: _static/img/gallery_analysis_volumetric.png
   :alt: An ELF isosurface with a color-mapped slice plane
   :width: 95%
   :figclass: screenshot

   Volumetric data — a live-isovalue ELF isosurface with a Miller-indexed
   slice plane.

.. figure:: _static/img/gallery_analysis_msg.png
   :alt: The magnetic space group dialog reporting a BNS label
   :width: 95%
   :figclass: screenshot

   Magnetic space group determination — BNS label, type, and the parent
   crystallographic group side by side.

Orchestration and monitoring
----------------------------

.. figure:: _static/img/gallery_workflow_canvas.png
   :alt: A node-graph pipeline chaining relaxation into band structure
   :width: 95%
   :figclass: screenshot

   The Orchestration canvas — a DAG of calculations passing geometries and ground
   states between nodes.

.. figure:: _static/img/gallery_jobs_live.png
   :alt: Live metric plots streaming from a running MD job
   :width: 95%
   :figclass: screenshot

   Live monitoring — energy, temperature, and pressure streaming from a
   running job, with the thermostat setpoint as a dashed reference.
