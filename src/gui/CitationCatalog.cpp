#include "gui/CitationCatalog.hpp"

#include <QCoreApplication>

namespace calango::gui {

const std::vector<Citation>& citationCatalog()
{
    static const std::vector<Citation> kCatalog = {
        // ---------------------------------------------------------------
        // Core
        // ---------------------------------------------------------------
        {QStringLiteral("Core"),
         QStringLiteral(
             "Larsen, A. H. <i>et al.</i> The atomic simulation "
             "environment&mdash;a Python library for working with atoms. "
             "<i>J. Phys. Condens. Matter</i> <b>29</b>, 273002 (2017)."),
         QStringLiteral("Every generated script builds its structure and "
                        "runs its calculator through ASE; this is the one "
                        "citation nearly every other entry below assumes "
                        "alongside its own."),
         QStringLiteral(R"(@article{Larsen2017,
  author  = {Larsen, Ask Hjorth and Mortensen, Jens J{\o}rgen and Blomqvist, Jakob and
             Castelli, Ivano E. and Christensen, Rune and Du{\l}ak, Marcin and Friis, Jesper and
             Groves, Michael N. and Hammer, Bj{\o}rk and Hargus, Cory and Hermes, Eric D. and
             Jennings, Paul C. and Jensen, Peter Bjerre and Kermode, James and Kitchin, John R. and
             Kolsbjerg, Esben Leonhard and Kubal, Joseph and Kaasbjerg, Kristen and Lysgaard, Steen and
             Maronsson, J{\'o}n Bergmann and Maxson, Tristan and Olsen, Thomas and Pastewka, Lars and
             Peterson, Andrew and Rostgaard, Carsten and Schi{\o}tz, Jakob and Sch{\"u}tt, Ole and
             Strange, Mikkel and Thygesen, Kristian S. and Vegge, Tejs and Vilhelmsen, Lasse and
             Walter, Michael and Zeng, Zhenhua and Jacobsen, Karsten W.},
  title     = {The atomic simulation environment---a {P}ython library for working with atoms},
  journal   = {Journal of Physics: Condensed Matter},
  volume    = {29},
  pages     = {273002},
  year      = {2017},
  doi       = {10.1088/1361-648X/aa680e},
  publisher = {IOP Publishing}
})"),
         QStringLiteral("Larsen2017")},
        {QStringLiteral("Core"),
         QStringLiteral(
             "Seixas, L. <i>Calango: a desktop application for "
             "computational materials science</i> (2026). "
             "Available at https://github.com/seixas-research/calango."),
         QStringLiteral(
             "Recommended citation for Calango itself &mdash; no separate "
             "paper exists yet, so this software entry is it."),
         QStringLiteral(R"(@software{SeixasCalango,
  author  = {Seixas, Leandro},
  title   = {Calango: a desktop application for computational materials science},
  year    = {2026},
  url     = {https://github.com/seixas-research/calango}
})"),
         QStringLiteral("SeixasCalango")},

        // ---------------------------------------------------------------
        // Databases
        // ---------------------------------------------------------------
        {QStringLiteral("Databases"),
         QStringLiteral(
             "Horton, M. K. <i>et al.</i> Accelerated data-driven materials "
             "science with the Materials Project. <i>Nat. Mater.</i> "
             "<b>24</b>, 1522&ndash;1532 (2025)."),
         QStringLiteral("The Materials Project's own current citation "
                        "guidance &mdash; no longer the 2013 APL Materials "
                        "commentary that circulated for over a decade."),
         QStringLiteral(R"(@article{Horton2025MaterialsProject,
  author  = {Horton, Matthew K. and Huck, Patrick and Yang, Ruo Xi and Munro, Jason M. and
             Dwaraknath, Shyam and Ganose, Alex M. and Kingsbury, Ryan S. and Wen, Mingjian and
             Shen, Jimmy X. and Mathis, Tyler S. and Kaplan, Aaron D. and Berket, Karlo and
             Riebesell, Janosh and George, Janine and Rosen, Andrew S. and
             Spotte-Smith, Evan W. C. and McDermott, Matthew J. and Cohen, Orion A. and
             Dunn, Alex and Kuner, Matthew C. and Rignanese, Gian-Marco and Petretto, Guido and
             Waroquiers, David and Griffin, Sinead M. and Neaton, Jeffrey B. and
             Chrzan, Daryl C. and Asta, Mark and Hautier, Geoffroy and Cholia, Shreyas and
             Ceder, Gerbrand and Ong, Shyue Ping and Jain, Anubhav and Persson, Kristin A.},
  title     = {Accelerated data-driven materials science with the Materials Project},
  journal   = {Nature Materials},
  volume    = {24},
  number    = {10},
  pages     = {1522--1532},
  year      = {2025},
  doi       = {10.1038/s41563-025-02272-0}
})"),
         QStringLiteral("Horton2025MaterialsProject")},
        {QStringLiteral("Databases"),
         QStringLiteral(
             "Haastrup, S. <i>et al.</i> The Computational 2D Materials "
             "Database: high-throughput modeling and discovery of "
             "atomically thin crystals. <i>2D Mater.</i> <b>5</b>, 042002 "
             "(2018)."),
         QStringLiteral("C2DB's own site asks that both this paper and the "
                        "progress paper below be cited together."),
         QStringLiteral(R"(@article{Haastrup2018C2DB,
  author  = {Haastrup, Sten and Strange, Mikkel and Pandey, Mohnish and Deilmann, Thorsten and
             Schmidt, Per S. and Hinsche, Nicki F. and Gjerding, Morten N. and Torelli, Daniele and
             Larsen, Peter M. and Riis-Jensen, Anders C. and Gath, Jakob and Jacobsen, Karsten W. and
             Mortensen, Jens J{\o}rgen and Olsen, Thomas and Thygesen, Kristian S.},
  title   = {The Computational 2D Materials Database: high-throughput modeling and
             discovery of atomically thin crystals},
  journal = {2D Materials},
  volume  = {5},
  number  = {4},
  pages   = {042002},
  year    = {2018},
  doi     = {10.1088/2053-1583/aacfc1}
})"),
         QStringLiteral("Haastrup2018C2DB")},
        {QStringLiteral("Databases"),
         QStringLiteral(
             "Gjerding, M. N. <i>et al.</i> Recent progress of the "
             "computational 2D materials database (C2DB). <i>2D Mater.</i> "
             "<b>8</b>, 044002 (2021)."),
         QString(),
         QStringLiteral(R"(@article{Gjerding2021C2DB,
  author  = {Gjerding, Morten Niklas and Taghizadeh, Alireza and Rasmussen, Asbj{\o}rn and
             Ali, Sajid and Bertoldo, Fabian and Deilmann, Thorsten and
             Kn{\o}sgaard, Nikolaj R{\o}rb{\ae}k and Kruse, Mads and
             Larsen, Ask Hjorth and Manti, Simone and Pedersen, Thomas Garm and
             Petralanda Holguin, Urko and Skovhus, Thorbj{\o}rn and Svendsen, Mark Kamper and
             Mortensen, Jens J{\o}rgen and Olsen, Thomas and Thygesen, Kristian Sommer},
  title   = {Recent progress of the computational 2D materials database (C2DB)},
  journal = {2D Materials},
  volume  = {8},
  number  = {4},
  pages   = {044002},
  year    = {2021},
  doi     = {10.1088/2053-1583/ac1059}
})"),
         QStringLiteral("Gjerding2021C2DB")},
        {QStringLiteral("Databases"),
         QStringLiteral(
             "Kim, S. <i>et al.</i> PubChem 2025 update. "
             "<i>Nucleic Acids Res.</i> <b>53</b>, D1516&ndash;D1525 "
             "(2025)."),
         QStringLiteral(
             "PubChem's own \"primary citation\", updated roughly yearly; "
             "this is the most recent as of writing."),
         QStringLiteral(R"(@article{Kim2025PubChem,
  author  = {Kim, Sunghwan and Chen, Jie and Cheng, Tiejun and Gindulyte, Asta and
             He, Jia and He, Siqian and Li, Qingliang and Shoemaker, Benjamin A. and
             Thiessen, Paul A. and Yu, Bo and Zaslavsky, Leonid and Zhang, Jian and
             Bolton, Evan E.},
  title   = {{PubChem} 2025 update},
  journal = {Nucleic Acids Research},
  volume  = {53},
  number  = {D1},
  pages   = {D1516--D1525},
  year    = {2025},
  doi     = {10.1093/nar/gkae1059}
})"),
         QStringLiteral("Kim2025PubChem")},

        // ---------------------------------------------------------------
        // Calculators
        // ---------------------------------------------------------------
        {QStringLiteral("Calculators"),
         QStringLiteral(
             "Mortensen, J. J. <i>et al.</i> GPAW: an open Python package "
             "for electronic-structure calculations. <i>J. Chem. Phys.</i> "
             "<b>160</b>, 092503 (2024)."),
         QStringLiteral("GPAW's own docs ask that this be cited together "
                        "with the ASE reference under Core, above."),
         QStringLiteral(R"(@article{Mortensen2024GPAW,
  author  = {Mortensen, Jens J{\o}rgen and Larsen, Ask Hjorth and Kuisma, Mikael and
             Ivanov, Aleksei V. and Taghizadeh, Alireza and Peterson, Andrew and
             Haldar, Anubhab and Dohn, Asmus Ougaard and Sch{\"a}fer, Christian and
             J{\'o}nsson, Elvar {\"O}rn and Hermes, Eric D. and Nilsson, Fredrik Andreas and
             Kastlunger, Georg and Levi, Gianluca and J{\'o}nsson, Hannes and
             H{\"a}kkinen, Hannu and Fojt, Jakub and Kangsabanik, Jiban and
             S{\o}dequist, Joachim and Lehtom{\"a}ki, Jouko and Heske, Julian and
             Enkovaara, Jussi and Winther, Kirsten Tr{\o}stup and Dulak, Marcin and
             Melander, Marko M. and Ovesen, Martin and Louhivuori, Martti and
             Walter, Michael and Gjerding, Morten and Lopez-Acevedo, Olga and
             Erhart, Paul and Warmbier, Robert and W{\"u}rdemann, Rolf and Kaappa, Sami and
             Latini, Simone and Boland, Tara Maria and Bligaard, Thomas and
             Skovhus, Thorbj{\o}rn and Susi, Toma and Maxson, Tristan and Rossi, Tuomas and
             Chen, Xi and Schmerwitz, Yorick Leonard A. and Schi{\o}tz, Jakob and
             Olsen, Thomas and Jacobsen, Karsten Wedel and Thygesen, Kristian Sommer},
  title   = {{GPAW}: An open {P}ython package for electronic-structure calculations},
  journal = {The Journal of Chemical Physics},
  volume  = {160},
  pages   = {092503},
  year    = {2024},
  doi     = {10.1063/5.0182685}
})"),
         QStringLiteral("Mortensen2024GPAW")},
        {QStringLiteral("Calculators"),
         QStringLiteral(
             "Giannozzi, P. <i>et al.</i> QUANTUM ESPRESSO: a modular and "
             "open-source software project for quantum simulations of "
             "materials. <i>J. Phys. Condens. Matter</i> <b>21</b>, 395502 "
             "(2009)."),
         QStringLiteral("Quantum ESPRESSO's own citation page lists this "
                        "founding paper together with the 2017 paper "
                        "below, without retiring either."),
         QStringLiteral(R"(@article{Giannozzi2009QE,
  author  = {Giannozzi, Paolo and Baroni, Stefano and Bonini, Nicola and Calandra, Matteo and
             Car, Roberto and Cavazzoni, Carlo and Ceresoli, Davide and Chiarotti, Guido L. and
             Cococcioni, Matteo and Dabo, Ismaila and Dal Corso, Andrea and de Gironcoli, Stefano and
             Fabris, Stefano and Fratesi, Guido and Gebauer, Ralph and Gerstmann, Uwe and
             Gougoussis, Christos and Kokalj, Anton and Lazzeri, Michele and Martin-Samos, Layla and
             Marzari, Nicola and Mauri, Francesco and Mazzarello, Riccardo and Paolini, Stefano and
             Pasquarello, Alfredo and Paulatto, Lorenzo and Sbraccia, Carlo and Scandolo, Sandro and
             Sclauzero, Gabriele and Seitsonen, Ari P. and Smogunov, Alexander and Umari, Paolo and
             Wentzcovitch, Renata M.},
  title   = {{QUANTUM ESPRESSO}: a modular and open-source software project for
             quantum simulations of materials},
  journal = {Journal of Physics: Condensed Matter},
  volume  = {21},
  pages   = {395502},
  year    = {2009},
  doi     = {10.1088/0953-8984/21/39/395502}
})"),
         QStringLiteral("Giannozzi2009QE")},
        {QStringLiteral("Calculators"),
         QStringLiteral(
             "Giannozzi, P. <i>et al.</i> Advanced capabilities for "
             "materials modelling with Quantum ESPRESSO. "
             "<i>J. Phys. Condens. Matter</i> <b>29</b>, 465901 (2017)."),
         QString(),
         QStringLiteral(R"(@article{Giannozzi2017QE,
  author  = {Giannozzi, P. and Andreussi, O. and Brumme, T. and Bunau, O. and
             Buongiorno Nardelli, M. and Calandra, M. and Car, R. and Cavazzoni, C. and
             Ceresoli, D. and Cococcioni, M. and Colonna, N. and Carnimeo, I. and
             Dal Corso, A. and de Gironcoli, S. and Delugas, P. and DiStasio, R. A. and
             Ferretti, A. and Floris, A. and Fratesi, G. and Fugallo, G. and Gebauer, R. and
             Gerstmann, U. and Giustino, F. and Gorni, T. and Jia, J. and Kawamura, M. and
             Ko, H.-Y. and Kokalj, A. and K{\"u}{\c{c}}{\"u}kbenli, E. and Lazzeri, M. and
             Marsili, M. and Marzari, N. and Mauri, F. and Nguyen, N. L. and Nguyen, H.-V. and
             Otero-de-la-Roza, A. and Paulatto, L. and Ponc{\'e}, S. and Rocca, D. and
             Sabatini, R. and Santra, B. and Schlipf, M. and Seitsonen, A. P. and
             Smogunov, A. and Timrov, I. and Thonhauser, T. and Umari, P. and Vast, N. and
             Wu, X. and Baroni, S.},
  title   = {Advanced capabilities for materials modelling with {Quantum ESPRESSO}},
  journal = {Journal of Physics: Condensed Matter},
  volume  = {29},
  pages   = {465901},
  year    = {2017},
  doi     = {10.1088/1361-648X/aa8f79}
})"),
         QStringLiteral("Giannozzi2017QE")},
        {QStringLiteral("Calculators"),
         QStringLiteral(
             "Kresse, G. &amp; Hafner, J. Ab initio molecular dynamics for "
             "liquid metals. <i>Phys. Rev. B</i> <b>47</b>, 558&ndash;561 "
             "(1993)."),
         QStringLiteral("VASP's baseline citation set (this and the four "
                        "entries following) has been unchanged for years, "
                        "per the VASP team."),
         QStringLiteral(R"(@article{KresseHafner1993,
  author  = {Kresse, G. and Hafner, J.},
  title   = {Ab initio molecular dynamics for liquid metals},
  journal = {Physical Review B},
  volume  = {47},
  pages   = {558--561},
  year    = {1993},
  doi     = {10.1103/PhysRevB.47.558}
})"),
         QStringLiteral("KresseHafner1993")},
        {QStringLiteral("Calculators"),
         QStringLiteral(
             "Kresse, G. &amp; Hafner, J. Ab initio molecular-dynamics "
             "simulation of the liquid-metal&ndash;amorphous-semiconductor "
             "transition in germanium. <i>Phys. Rev. B</i> <b>49</b>, "
             "14251&ndash;14269 (1994)."),
         QString(),
         QStringLiteral(R"(@article{KresseHafner1994,
  author  = {Kresse, G. and Hafner, J.},
  title   = {Ab initio molecular-dynamics simulation of the liquid-metal--amorphous-semiconductor
             transition in germanium},
  journal = {Physical Review B},
  volume  = {49},
  pages   = {14251--14269},
  year    = {1994},
  doi     = {10.1103/PhysRevB.49.14251}
})"),
         QStringLiteral("KresseHafner1994")},
        {QStringLiteral("Calculators"),
         QStringLiteral(
             "Kresse, G. &amp; Furthm&uuml;ller, J. Efficiency of ab-initio "
             "total energy calculations for metals and semiconductors "
             "using a plane-wave basis set. <i>Comput. Mater. Sci.</i> "
             "<b>6</b>, 15&ndash;50 (1996)."),
         QString(),
         QStringLiteral(R"(@article{KresseFurthmuller1996CMS,
  author  = {Kresse, G. and Furthm{\"u}ller, J.},
  title   = {Efficiency of ab-initio total energy calculations for metals and
             semiconductors using a plane-wave basis set},
  journal = {Computational Materials Science},
  volume  = {6},
  pages   = {15--50},
  year    = {1996},
  doi     = {10.1016/0927-0256(96)00008-0}
})"),
         QStringLiteral("KresseFurthmuller1996CMS")},
        {QStringLiteral("Calculators"),
         QStringLiteral(
             "Kresse, G. &amp; Furthm&uuml;ller, J. Efficient iterative "
             "schemes for ab initio total-energy calculations using a "
             "plane-wave basis set. <i>Phys. Rev. B</i> <b>54</b>, "
             "11169&ndash;11186 (1996)."),
         QString(),
         QStringLiteral(R"(@article{KresseFurthmuller1996PRB,
  author  = {Kresse, G. and Furthm{\"u}ller, J.},
  title   = {Efficient iterative schemes for ab initio total-energy calculations
             using a plane-wave basis set},
  journal = {Physical Review B},
  volume  = {54},
  pages   = {11169--11186},
  year    = {1996},
  doi     = {10.1103/PhysRevB.54.11169}
})"),
         QStringLiteral("KresseFurthmuller1996PRB")},
        {QStringLiteral("Calculators"),
         QStringLiteral(
             "Kresse, G. &amp; Joubert, D. From ultrasoft pseudopotentials "
             "to the projector augmented-wave method. <i>Phys. Rev. B</i> "
             "<b>59</b>, 1758&ndash;1775 (1999)."),
         QStringLiteral("Needed whenever the PAW potentials are used "
                        "&mdash; VASP's default and by far the common "
                        "case."),
         QStringLiteral(R"(@article{KresseJoubert1999,
  author  = {Kresse, G. and Joubert, D.},
  title   = {From ultrasoft pseudopotentials to the projector augmented-wave method},
  journal = {Physical Review B},
  volume  = {59},
  pages   = {1758--1775},
  year    = {1999},
  doi     = {10.1103/PhysRevB.59.1758}
})"),
         QStringLiteral("KresseJoubert1999")},
        {QStringLiteral("Calculators"),
         QStringLiteral(
             "Soler, J. M. <i>et al.</i> The SIESTA method for <i>ab "
             "initio</i> order-<i>N</i> materials simulation. "
             "<i>J. Phys. Condens. Matter</i> <b>14</b>, 2745&ndash;2779 "
             "(2002)."),
         QStringLiteral("SIESTA's own citation page asks that both this "
                        "paper and the 2020 update below be cited "
                        "together."),
         QStringLiteral(R"(@article{Soler2002siesta,
  author  = {Soler, Jos{\'e} M. and Artacho, Emilio and Gale, Julian D. and
             Garc{\'i}a, Alberto and Junquera, Javier and Ordej{\'o}n, Pablo and
             S{\'a}nchez-Portal, Daniel},
  title   = {The {SIESTA} method for {\it ab initio} order-{\it N} materials simulation},
  journal = {Journal of Physics: Condensed Matter},
  volume  = {14},
  pages   = {2745--2779},
  year    = {2002},
  doi     = {10.1088/0953-8984/14/11/302}
})"),
         QStringLiteral("Soler2002siesta")},
        {QStringLiteral("Calculators"),
         QStringLiteral(
             "Garc&iacute;a, A. <i>et al.</i> SIESTA: recent developments "
             "and applications. <i>J. Chem. Phys.</i> <b>152</b>, 204108 "
             "(2020)."),
         QString(),
         QStringLiteral(R"(@article{Garcia2020siesta,
  author  = {Garc{\'i}a, Alberto and Papior, Nick and Akhtar, Arsalan and Artacho, Emilio and
             Blum, Volker and Bosoni, Emanuele and Brandimarte, Pedro and Brandbyge, Mads and
             Cerd{\'a}, J. I. and Corsetti, Fabiano and Cuadrado, Ram{\'o}n and Dikan, Vladimir and
             Ferrer, Jaime and Gale, Julian and Garc{\'i}a-Fern{\'a}ndez, Pablo and
             Garc{\'i}a-Su{\'a}rez, V. M. and Garc{\'i}a, Sandra and Huhs, Georg and
             Illera, Sergio and Koryt{\'a}r, Richard and Koval, Peter and Lebedeva, Irina and
             Lin, Lin and L{\'o}pez-Tarifa, Pablo and Mayo, Sara G. and Mohr, Stephan and
             Ordej{\'o}n, Pablo and Postnikov, Andrei and Pouillon, Yann and Pruneda, Miguel and
             Robles, Roberto and S{\'a}nchez-Portal, Daniel and Soler, Jos{\'e} M. and
             Ullah, Rafi and Yu, Victor Wen-zhe and Junquera, Javier},
  title   = {Siesta: Recent developments and applications},
  journal = {The Journal of Chemical Physics},
  volume  = {152},
  number  = {20},
  pages   = {204108},
  year    = {2020},
  doi     = {10.1063/5.0005077}
})"),
         QStringLiteral("Garcia2020siesta")},
        {QStringLiteral("Calculators"),
         QStringLiteral(
             "Thompson, A. P. <i>et al.</i> LAMMPS&mdash;a flexible "
             "simulation tool for particle-based materials modeling at "
             "the atomic, meso, and continuum scales. "
             "<i>Comput. Phys. Commun.</i> <b>271</b>, 108171 (2022)."),
         QString(),
         QStringLiteral(R"(@article{Thompson2022lammps,
  author  = {Thompson, Aidan P. and Aktulga, H. Metin and Berger, Richard and
             Bolintineanu, Dan S. and Brown, W. Michael and Crozier, Paul S. and
             in 't Veld, Pieter J. and Kohlmeyer, Axel and Moore, Stan G. and
             Nguyen, Trung Dac and Shan, Ray and Stevens, Mark J. and Tranchida, Julien and
             Trott, Christian and Plimpton, Steven J.},
  title   = {{LAMMPS} - a flexible simulation tool for particle-based materials
             modeling at the atomic, meso, and continuum scales},
  journal = {Computer Physics Communications},
  volume  = {271},
  pages   = {108171},
  year    = {2022},
  doi     = {10.1016/j.cpc.2021.108171}
})"),
         QStringLiteral("Thompson2022lammps")},
        {QStringLiteral("Calculators"),
         QStringLiteral(
             "Batatia, I., Kov&aacute;cs, D. P., Simm, G. N. C., Ortner, "
             "C. &amp; Cs&aacute;nyi, G. MACE: higher order equivariant "
             "message passing neural networks for fast and accurate force "
             "fields. <i>Adv. Neural Inf. Process. Syst.</i> <b>35</b> "
             "(2022)."),
         QStringLiteral("MACE's own README asks for this and the design-"
                        "space paper below together."),
         QStringLiteral(R"(@inproceedings{Batatia2022mace,
  title     = {{MACE}: Higher Order Equivariant Message Passing Neural Networks for
               Fast and Accurate Force Fields},
  author    = {Batatia, Ilyes and Kov{\'a}cs, D{\'a}vid P{\'e}ter and Simm, Gregor N. C. and
               Ortner, Christoph and Cs{\'a}nyi, G{\'a}bor},
  booktitle = {Advances in Neural Information Processing Systems},
  volume    = {35},
  year      = {2022},
  url       = {https://openreview.net/forum?id=YPpSngE-ZU}
})"),
         QStringLiteral("Batatia2022mace")},
        {QStringLiteral("Calculators"),
         QStringLiteral(
             "Batatia, I. <i>et al.</i> The design space of "
             "E(3)-equivariant atom-centered interatomic potentials. "
             "Preprint at <i>arXiv</i> 2205.06643 (2022)."),
         QString(),
         QStringLiteral(R"(@misc{Batatia2022Design,
  title         = {The Design Space of E(3)-Equivariant Atom-Centered Interatomic Potentials},
  author        = {Batatia, Ilyes and Batzner, Simon and Kov{\'a}cs, D{\'a}vid P{\'e}ter and
                   Musaelian, Albert and Simm, Gregor N. C. and Drautz, Ralf and
                   Ortner, Christoph and Kozinsky, Boris and Cs{\'a}nyi, G{\'a}bor},
  year          = {2022},
  eprint        = {2205.06643},
  eprinttype    = {arxiv},
  doi           = {10.48550/arXiv.2205.06643}
})"),
         QStringLiteral("Batatia2022Design")},
        {QStringLiteral("Calculators"),
         QStringLiteral(
             "Bannwarth, C. <i>et al.</i> Extended tight-binding quantum "
             "chemistry methods. <i>WIREs Comput. Mol. Sci.</i> <b>11</b>, "
             "e01493 (2020)."),
         QStringLiteral("xtb's general reference, required regardless of "
                        "which GFN method is selected."),
         QStringLiteral(R"(@article{Bannwarth2020xtb,
  author  = {Bannwarth, Christoph and Caldeweyher, Eike and Ehlert, Sebastian and
             Hansen, Andreas and Pracht, Philipp and Seibert, Jakob and
             Spicher, Sebastian and Grimme, Stefan},
  title   = {Extended tight-binding quantum chemistry methods},
  journal = {WIREs Computational Molecular Science},
  volume  = {11},
  number  = {2},
  pages   = {e01493},
  year    = {2020},
  doi     = {10.1002/wcms.1493}
})"),
         QStringLiteral("Bannwarth2020xtb")},
        {QStringLiteral("Calculators"),
         QStringLiteral(
             "Bannwarth, C., Ehlert, S. &amp; Grimme, S. GFN2-xTB&mdash;an "
             "accurate and broadly parametrized self-consistent "
             "tight-binding quantum chemical method with multipole "
             "electrostatics and density-dependent dispersion "
             "contributions. <i>J. Chem. Theory Comput.</i> <b>15</b>, "
             "1652&ndash;1671 (2019)."),
         QStringLiteral("For GFN2-xTB, Calango's default xtb method; "
                        "GFN1-xTB or GFN-FF each have their own paper "
                        "instead, per xtb's README."),
         QStringLiteral(R"(@article{Bannwarth2019gfn2,
  author  = {Bannwarth, Christoph and Ehlert, Sebastian and Grimme, Stefan},
  title   = {{GFN2-xTB}---An Accurate and Broadly Parametrized Self-Consistent
             Tight-Binding Quantum Chemical Method with Multipole Electrostatics
             and Density-Dependent Dispersion Contributions},
  journal = {Journal of Chemical Theory and Computation},
  volume  = {15},
  pages   = {1652--1671},
  year    = {2019},
  doi     = {10.1021/acs.jctc.8b01176}
})"),
         QStringLiteral("Bannwarth2019gfn2")},
        {QStringLiteral("Calculators"),
         QStringLiteral(
             "Elstner, M. <i>et al.</i> Self-consistent-charge "
             "density-functional tight-binding method for simulations of "
             "complex materials properties. <i>Phys. Rev. B</i> "
             "<b>58</b>, 7260&ndash;7268 (1998)."),
         QStringLiteral("The SCC-DFTB (DFTB2) method DFTB+ implements. The "
                        "Slater-Koster parameter set used (mio, 3ob, pbc, "
                        "matsci, …) carries its own separate citation "
                        "requirement, stated on its own download page at "
                        "dftb.org."),
         QStringLiteral(R"(@article{Elstner1998sccdftb,
  author  = {Elstner, M. and Porezag, D. and Jungnickel, G. and Elsner, J. and
             Haugk, M. and Frauenheim, Th. and Suhai, S. and Seifert, G.},
  title   = {Self-consistent-charge density-functional tight-binding method
             for simulations of complex materials properties},
  journal = {Physical Review B},
  volume  = {58},
  pages   = {7260--7268},
  year    = {1998},
  doi     = {10.1103/PhysRevB.58.7260}
})"),
         QStringLiteral("Elstner1998sccdftb")},

        // ---------------------------------------------------------------
        // Libraries
        // ---------------------------------------------------------------
        {QStringLiteral("Libraries"),
         QStringLiteral(
             "Togo, A., Shinohara, K. &amp; Tanaka, I. Spglib: a software "
             "library for crystal symmetry search. <i>Sci. Technol. Adv. "
             "Mater. Methods</i> <b>4</b>, 2384822 (2024)."),
         QString(),
         QStringLiteral(R"(@article{Togo2024spglib,
  author    = {Togo, Atsushi and Shinohara, Kohei and Tanaka, Isao},
  title     = {Spglib: a software library for crystal symmetry search},
  journal   = {Science and Technology of Advanced Materials: Methods},
  volume    = {4},
  number    = {1},
  pages     = {2384822--2384836},
  year      = {2024},
  doi       = {10.1080/27660400.2024.2384822}
})"),
         QStringLiteral("Togo2024spglib")},
        {QStringLiteral("Libraries"),
         QStringLiteral(
             "Harris, C. R. <i>et al.</i> Array programming with NumPy. "
             "<i>Nature</i> <b>585</b>, 357&ndash;362 (2020)."),
         QString(),
         QStringLiteral(R"(@article{Harris2020,
  author    = {Harris, Charles R. and Millman, K. Jarrod and van der Walt, St{\'e}fan J. and
               Gommers, Ralf and Virtanen, Pauli and Cournapeau, David and Wieser, Eric and
               Taylor, Julian and Berg, Sebastian and Smith, Nathaniel J. and Kern, Robert and
               Picus, Matti and Hoyer, Stephan and van Kerkwijk, Marten H. and Brett, Matthew and
               Haldane, Allan and Fern{\'a}ndez del R{\'i}o, Jaime and Wiebe, Mark and
               Peterson, Pearu and G{\'e}rard-Marchant, Pierre and Sheppard, Kevin and
               Reddy, Tyler and Weckesser, Warren and Abbasi, Hameer and Gohlke, Christoph and
               Oliphant, Travis E.},
  title     = {Array programming with {NumPy}},
  journal   = {Nature},
  volume    = {585},
  number    = {7825},
  pages     = {357--362},
  year      = {2020},
  doi       = {10.1038/s41586-020-2649-2}
})"),
         QStringLiteral("Harris2020")},
        {QStringLiteral("Libraries"),
         QStringLiteral(
             "Togo, A., Chaput, L., Tadano, T. &amp; Tanaka, I. "
             "Implementation strategies in phonopy and phono3py. "
             "<i>J. Phys. Condens. Matter</i> <b>35</b>, 353001 (2023)."),
         QStringLiteral("phonopy's docs ask that this and the paper below "
                        "be cited together &mdash; no longer the single "
                        "2015 Scripta Materialia paper."),
         QStringLiteral(R"(@article{Togo2023Implementation,
  author    = {Togo, Atsushi and Chaput, Laurent and Tadano, Terumasa and Tanaka, Isao},
  title     = {Implementation strategies in phonopy and phono3py},
  journal   = {Journal of Physics: Condensed Matter},
  volume    = {35},
  number    = {35},
  pages     = {353001},
  year      = {2023},
  doi       = {10.1088/1361-648X/acd831}
})"),
         QStringLiteral("Togo2023Implementation")},
        {QStringLiteral("Libraries"),
         QStringLiteral(
             "Togo, A. First-principles phonon calculations with phonopy "
             "and phono3py. <i>J. Phys. Soc. Jpn.</i> <b>92</b>, 012001 "
             "(2023)."),
         QString(),
         QStringLiteral(R"(@article{Togo2023FirstPrinciples,
  author    = {Togo, Atsushi},
  title     = {First-principles phonon calculations with phonopy and phono3py},
  journal   = {Journal of the Physical Society of Japan},
  volume    = {92},
  number    = {1},
  pages     = {012001},
  year      = {2023},
  doi       = {10.7566/JPSJ.92.012001}
})"),
         QStringLiteral("Togo2023FirstPrinciples")},
        {QStringLiteral("Libraries"),
         QStringLiteral(
             "&Aring;ngqvist, M. <i>et al.</i> ICET&mdash;a Python library "
             "for constructing and sampling alloy cluster expansions. "
             "<i>Adv. Theory Simul.</i> <b>2</b>, 1900015 (2019)."),
         QString(),
         QStringLiteral(R"(@article{Angqvist2019icet,
  author  = {{\r{A}}ngqvist, Mattias and Mu{\~n}oz, William A. and Rahm, J. Magnus and
             Fransson, Erik and Durniak, C{\'e}line and Rozyczko, Piotr and
             Rod, Thomas H. and Erhart, Paul},
  title   = {{ICET} -- A Python Library for Constructing and Sampling Alloy Cluster Expansions},
  journal = {Advanced Theory and Simulations},
  volume  = {2},
  number  = {7},
  pages   = {1900015},
  year    = {2019},
  doi     = {10.1002/adts.201900015}
})"),
         QStringLiteral("Angqvist2019icet")},
        {QStringLiteral("Libraries"),
         QStringLiteral(
             "Caldeweyher, E., Bannwarth, C. &amp; Grimme, S. Extension of "
             "the D3 dispersion coefficient model. <i>J. Chem. Phys.</i> "
             "<b>147</b>, 034112 (2017)."),
         QStringLiteral("dftd4's README lists this and the two entries "
                        "following as always-cite, for any use of the "
                        "D4 dispersion correction."),
         QStringLiteral(R"(@article{Caldeweyher2017,
  author  = {Caldeweyher, Eike and Bannwarth, Christoph and Grimme, Stefan},
  title   = {Extension of the D3 dispersion coefficient model},
  journal = {The Journal of Chemical Physics},
  volume  = {147},
  number  = {3},
  pages   = {034112},
  year    = {2017},
  doi     = {10.1063/1.4993215}
})"),
         QStringLiteral("Caldeweyher2017")},
        {QStringLiteral("Libraries"),
         QStringLiteral(
             "Caldeweyher, E. <i>et al.</i> A generally applicable "
             "atomic-charge dependent London dispersion correction. "
             "<i>J. Chem. Phys.</i> <b>150</b>, 154122 (2019)."),
         QString(),
         QStringLiteral(R"(@article{Caldeweyher2019,
  author  = {Caldeweyher, Eike and Ehlert, Sebastian and Hansen, Andreas and
             Neugebauer, Hagen and Spicher, Sebastian and Bannwarth, Christoph and
             Grimme, Stefan},
  title   = {A generally applicable atomic-charge dependent London dispersion correction},
  journal = {The Journal of Chemical Physics},
  volume  = {150},
  number  = {15},
  pages   = {154122},
  year    = {2019},
  doi     = {10.1063/1.5090222}
})"),
         QStringLiteral("Caldeweyher2019")},
        {QStringLiteral("Libraries"),
         QStringLiteral(
             "Caldeweyher, E., Mewes, J.-M., Ehlert, S. &amp; Grimme, S. "
             "Extension and evaluation of the D4 London-dispersion model "
             "for periodic systems. <i>Phys. Chem. Chem. Phys.</i> "
             "<b>22</b>, 8499&ndash;8512 (2020)."),
         QString(),
         QStringLiteral(R"(@article{Caldeweyher2020,
  author  = {Caldeweyher, Eike and Mewes, Jan-Michael and Ehlert, Sebastian and Grimme, Stefan},
  title   = {Extension and evaluation of the {D4} London-dispersion model for periodic systems},
  journal = {Physical Chemistry Chemical Physics},
  volume  = {22},
  number  = {16},
  pages   = {8499--8512},
  year    = {2020},
  doi     = {10.1039/D0CP00502A}
})"),
         QStringLiteral("Caldeweyher2020")},
    };
    return kCatalog;
}

} // namespace calango::gui
