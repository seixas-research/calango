#include "core/EngineCalculatorBlocks.hpp"

#include "core/PhysicalConstants.hpp"

#include <sstream>
#include <string>
#include <vector>

namespace calango::core {

namespace EngineBlocks {

namespace {

/// Rydberg per electronvolt — several of these codes read cutoffs in Ry while
/// the UI collects eV, and a stray factor of 13.6 in a cutoff is invisible
/// (the run completes, just badly converged or absurdly expensive).
constexpr double kRydbergPerEv = 1.0 / 13.605693122994;
/// Hartree per electronvolt, for the tolerances the same codes take in Ha.
constexpr double kHartreePerEv = 1.0 / kHartreeEv;

std::string trim(std::string text)
{
    const auto begin = text.find_first_not_of(" \t\r");
    const auto end = text.find_last_not_of(" \t\r");
    return begin == std::string::npos ? std::string()
                                      : text.substr(begin, end - begin + 1);
}

/// Non-empty, comment-stripped lines of a free-form "extra settings" field.
std::vector<std::string> extraLines(const std::string& text)
{
    std::vector<std::string> lines;
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        const std::string entry = trim(line.substr(0, line.find('#')));
        if (!entry.empty())
            lines.push_back(entry);
    }
    return lines;
}

/// A Python `key=value` pair from a "key value" or "key = value" line.
/// Numbers are emitted bare; anything else is quoted, so a free-form field
/// cannot produce a script that will not parse.
void emitExtraKeyword(std::ostringstream& out, const std::string& entry,
                      const char* indent)
{
    const auto split = entry.find_first_of(" \t=");
    if (split == std::string::npos)
        return;
    const std::string key = trim(entry.substr(0, split));
    std::string value = trim(entry.substr(split + 1));
    if (!value.empty() && value.front() == '=')
        value = trim(value.substr(1));
    if (key.empty() || value.empty())
        return;
    const bool numeric =
        value.find_first_not_of("0123456789+-.eEdD") == std::string::npos;
    out << indent << key << "=" << (numeric ? value : "\"" + value + "\"")
        << ",\n";
}

/// The task note every engine that cannot be driven by an ASE optimizer shares.
void emitAseDrivenTasksOnly(std::ostringstream& out, const char* engine)
{
    out << "# NOTE: " << engine << " returns energy and forces to ASE, so the\n"
           "# ASE optimizers, molecular dynamics and vibrational modules all\n"
           "# drive it normally — the run below is a plain ASE workflow.\n";
}

} // namespace

// ---------------------------------------------------------------------------
// ABINIT
// ---------------------------------------------------------------------------
void emitAbinit(std::ostringstream& out, const CalculatorConfig& c)
{
    out << "# --- ABINIT -------------------------------------------------------\n"
           "#\n"
           "# Plane-wave / PAW DFT through ase.calculators.abinit, which writes\n"
           "# abinit.abi, runs the `abinit` binary and reads the results back.\n"
           "#\n"
           "# The pseudopotentials ARE the calculation. `pps` below names the\n"
           "# FAMILY of tables (fhi = norm-conserving Fritz-Haber, paw / jth =\n"
           "# PAW datasets, hgh = Hartwigsen-Goedecker-Hutter, tm =\n"
           "# Troullier-Martins), and ABINIT looks each element up in the\n"
           "# directories below. A `pps` naming a family that is not installed\n"
           "# fails at the first element, not at the end of the SCF.\n"
           "import os\n"
           "from ase.calculators.abinit import Abinit\n"
           "\n";
    if (!c.abinitPseudoDir.empty()) {
        out << "pp_paths = [r\"" << c.abinitPseudoDir << "\"]\n\n";
    } else {
        out << "# EDIT ME: no pseudopotential directory is configured. Point\n"
               "# this at the table set (Preferences -> External Files), or\n"
               "# leave it None to let ASE read its own ~/.config/ase/config.ini.\n"
               "pp_paths = None\n\n";
    }
    out << "atoms.calc = Abinit(\n"
        << "    xc=\"" << c.abinitXc << "\",\n"
        // ASE's Abinit takes ecut in eV and converts to Hartree itself, so the
        // shared plane-wave cutoff passes straight through — the same number
        // that means ENCUT for VASP and PW(ecut) for GPAW.
        << "    ecut=" << c.planeWaveCutoffEv << ",  # eV (ASE converts to Ha)\n"
        << "    pps=\"" << c.abinitPps << "\",\n"
        << "    kpts=(" << c.kpts[0] << ", " << c.kpts[1] << ", " << c.kpts[2]
        << "),\n"
        << "    toldfe=" << c.abinitToldfe
        << ",  # Ha — SCF total-energy tolerance\n"
        << "    nstep=" << c.abinitNstep << ",\n";
    if (c.spinPolarized)
        // nsppol = 2 is the collinear spin-polarized run; the per-atom moments
        // ride on the Atoms object and ASE writes them as spinat.
        out << "    nsppol=2,  # collinear spin polarization\n";
    if (c.smearing != SmearingMethod::None && smearingUsesWidth(c.smearing))
        out << "    occopt=3,  # Fermi-Dirac occupations\n"
            << "    tsmear=" << c.smearingWidthEv * kHartreePerEv
            << ",  # Ha (" << c.smearingWidthEv << " eV)\n";
    for (const std::string& entry : extraLines(c.abinitExtra))
        emitExtraKeyword(out, entry, "    ");
    if (!c.abinitPseudoDir.empty())
        out << "    pp_paths=pp_paths,\n";
    out << ")\n";
    emitAseDrivenTasksOnly(out, "ABINIT");
}

// ---------------------------------------------------------------------------
// FHI-aims
// ---------------------------------------------------------------------------
void emitAims(std::ostringstream& out, const CalculatorConfig& c)
{
    out << "# --- FHI-aims -----------------------------------------------------\n"
           "#\n"
           "# All-electron DFT in numeric atom-centred orbitals, through\n"
           "# ase.calculators.aims.\n"
           "#\n"
           "# There is NO plane-wave cutoff here, and nothing in this block is\n"
           "# one. The basis is the SPECIES DEFAULTS tier: light /\n"
           "# intermediate / tight / really_tight are pre-tabulated,\n"
           "# hierarchical basis + integration-grid + accuracy sets, and moving\n"
           "# up a tier is how an aims calculation is converged. `light` is the\n"
           "# production default for geometries; `tight` is what a published\n"
           "# energy wants.\n"
           "import os\n"
           "from ase.calculators.aims import Aims, AimsProfile\n"
           "\n";
    if (!c.aimsSpeciesDir.empty()) {
        out << "species_dir = os.path.join(r\"" << c.aimsSpeciesDir << "\", \""
            << c.aimsSpeciesTier << "\")\n";
    } else {
        out << "# EDIT ME: no species_defaults directory is configured. It ships\n"
               "# with FHI-aims as `species_defaults/defaults_2020/<tier>`.\n"
               "species_dir = os.path.join(\n"
               "    os.environ.get(\"AIMS_SPECIES_DIR\", \"/path/to/species_defaults\"),\n"
            << "    \"" << c.aimsSpeciesTier << "\")\n";
    }
    out << "\n"
           "atoms.calc = Aims(\n"
           "    profile=AimsProfile(command=os.environ.get(\n"
           "        \"ASE_AIMS_COMMAND\", \"aims.x\")),\n"
           "    species_dir=species_dir,\n"
        << "    xc=\"" << c.aimsXc << "\",\n"
        << "    relativistic=\"" << c.aimsRelativistic << "\",\n"
        << "    sc_accuracy_etot=" << c.aimsScfAccuracyEv << ",  # eV\n"
           ")\n"
           "\n";
    // aims decides periodicity from the Atoms object; a k_grid on a molecule is
    // rejected outright, so it is written conditionally rather than always.
    // Everything below is therefore .set() on the constructed calculator, not
    // another constructor argument.
    out << "if any(atoms.pbc):\n"
           "    atoms.calc.set(k_grid=("
        << c.kpts[0] << ", " << c.kpts[1] << ", " << c.kpts[2] << "))\n";
    if (c.spinPolarized) {
        out << "atoms.calc.set(spin=\"collinear\",\n"
               "               default_initial_moment="
            << c.initialMagMoment << ")\n";
    }
    if (c.smearing != SmearingMethod::None && smearingUsesWidth(c.smearing))
        out << "atoms.calc.set(occupation_type=\"gaussian "
            << c.smearingWidthEv << "\")  # eV\n";
    for (const std::string& entry : extraLines(c.aimsExtra)) {
        std::ostringstream keyword;
        emitExtraKeyword(keyword, entry, "");
        std::string text = keyword.str();
        if (text.size() > 2)
            out << "atoms.calc.set(" << text.substr(0, text.size() - 2) << ")\n";
    }
    emitAseDrivenTasksOnly(out, "FHI-aims");
}

// ---------------------------------------------------------------------------
// NWChem
// ---------------------------------------------------------------------------
void emitNwChem(std::ostringstream& out, const CalculatorConfig& c)
{
    // pspw / band / paw are the PLANE-WAVE modules — the only ones that treat
    // the cell as periodic. Everything else is a Gaussian-basis molecular
    // method that ignores the lattice entirely.
    const bool planeWave = c.nwchemTheory == "pspw" || c.nwchemTheory == "band"
        || c.nwchemTheory == "paw";
    out << "# --- NWChem -------------------------------------------------------\n"
           "#\n"
           "# NWChem is two codes in one binary, and `theory` decides which:\n"
           "#\n"
           "#   dft / scf / mp2 / ccsd / tce   Gaussian-basis MOLECULAR methods.\n"
           "#                                  They ignore the unit cell.\n"
           "#   pspw / band / paw              plane-wave PERIODIC DFT. These\n"
           "#                                  ignore `basis` instead.\n"
           "#\n"
           "# Running a molecular theory on a crystal is the standard way an\n"
           "# NWChem input comes out quietly wrong: it completes, and reports\n"
           "# the energy of an isolated cluster.\n"
           "from ase.calculators.nwchem import NWChem\n"
           "\n";
    if (planeWave)
        out << "# theory=\"" << c.nwchemTheory
            << "\" is a plane-wave module: the cell is\n"
               "# treated periodically and `basis` is not read.\n";
    else
        out << "# theory=\"" << c.nwchemTheory
            << "\" is a molecular module: the Gaussian basis\n"
               "# below IS the basis set, and the unit cell is ignored.\n";
    out << "atoms.calc = NWChem(\n"
        << "    theory=\"" << c.nwchemTheory << "\",\n";
    if (c.nwchemTheory == "dft")
        out << "    xc=\"" << c.nwchemXc << "\",\n";
    if (!planeWave)
        out << "    basis=\"" << c.nwchemBasis << "\",\n";
    else
        out << "    kpts=(" << c.kpts[0] << ", " << c.kpts[1] << ", "
            << c.kpts[2] << "),\n";
    if (c.charge != 0)
        out << "    charge=" << c.charge << ",\n";
    if (!c.nwchemMemory.empty())
        out << "    memory=\"" << c.nwchemMemory << "\",\n";
    if (c.multiplicity != 1 && c.nwchemTheory == "dft")
        // NWChem spells an open shell as `odft` + `mult` inside the dft block,
        // not as a top-level keyword.
        out << "    dft={\"odft\": None, \"mult\": " << c.multiplicity << "},\n";
    for (const std::string& entry : extraLines(c.nwchemExtra))
        emitExtraKeyword(out, entry, "    ");
    out << ")\n";
    emitAseDrivenTasksOnly(out, "NWChem");
}

// ---------------------------------------------------------------------------
// OpenMX
// ---------------------------------------------------------------------------
void emitOpenMx(std::ostringstream& out, const CalculatorConfig& c)
{
    out << "# --- OpenMX -------------------------------------------------------\n"
           "#\n"
           "# DFT in pseudo-atomic orbitals, through ase.calculators.openmx.\n"
           "#\n"
           "# Like SIESTA, OpenMX has NO plane-wave basis cutoff. The\n"
           "# `energy_cutoff` below is scf.energycutoff: the REAL-SPACE grid\n"
           "# the Hartree and exchange-correlation terms are integrated on.\n"
           "# Raising it refines that grid; it does not enlarge the basis. The\n"
           "# basis is the PAO set OpenMX picks per element out of DFT_DATA_PATH\n"
           "# (e.g. `Fe6.0S-s2p2d1`), and its quality is chosen there.\n"
           "import os\n"
           "from ase.calculators.openmx import OpenMX\n"
           "\n";
    if (!c.openmxDataPath.empty())
        out << "os.environ.setdefault(\"OPENMX_DFT_DATA_PATH\", r\""
            << c.openmxDataPath << "\")\n\n";
    else
        out << "# EDIT ME: no DFT_DATA_PATH is configured. It is the\n"
               "# `DFT_DATA<version>` directory shipped with OpenMX, holding the\n"
               "# VPS pseudopotentials and the PAO basis databases.\n"
               "os.environ.setdefault(\"OPENMX_DFT_DATA_PATH\",\n"
               "                      \"/path/to/openmx/DFT_DATA19\")\n\n";
    out << "atoms.calc = OpenMX(\n"
           "    label=\"openmx\",\n"
        << "    xc=\"" << c.openmxXc << "\",\n"
        // ASE's OpenMX calculator takes energy_cutoff in eV and writes Ry.
        << "    energy_cutoff=" << c.openmxEnergyCutoffEv
        << ",  # eV — the REAL-SPACE grid ("
        << c.openmxEnergyCutoffEv * kRydbergPerEv << " Ry)\n"
        << "    kpts=(" << c.kpts[0] << ", " << c.kpts[1] << ", " << c.kpts[2]
        << "),\n"
        << "    scf_criterion=" << c.openmxScfCriterionEv << ",  # eV\n"
        << "    scf_maxiter=" << c.openmxScfMaxIter << ",\n"
        << "    eigensolver=\"" << c.openmxEigenSolver << "\",\n";
    if (c.spinPolarized)
        out << "    scf_spinpolarization=\"on\",\n";
    out << ")\n";
    emitAseDrivenTasksOnly(out, "OpenMX");
}

// ---------------------------------------------------------------------------
// FLEUR
// ---------------------------------------------------------------------------
void emitFleur(std::ostringstream& out, const CalculatorConfig& c)
{
    out << "# --- FLEUR --------------------------------------------------------\n"
           "#\n"
           "# Full-potential linearized augmented plane wave (FLAPW).\n"
           "#\n"
           "# ASE's own ase.calculators.fleur is a STUB that raises: modern\n"
           "# FLEUR support lives in the separate `ase-fleur` package\n"
           "#     pip install ase-fleur\n"
           "# which registers a Fleur calculator against the same\n"
           "# GenericFileIOCalculator machinery every other ASE code uses.\n"
           "#\n"
           "# The import below tries the two module layouts that package has\n"
           "# shipped rather than assuming one, and fails with a message that\n"
           "# says what to install instead of an ImportError three frames deep.\n"
           "import os\n"
           "\n"
           "try:\n"
           "    from ase_fleur import Fleur, FleurProfile\n"
           "except ImportError:\n"
           "    try:\n"
           "        from ase_fleur.calculator import Fleur, FleurProfile\n"
           "    except ImportError as error:\n"
           "        raise ImportError(\n"
           "            \"FLEUR support needs the ase-fleur package:\\n\"\n"
           "            \"    pip install ase-fleur\\n\"\n"
           "            \"(ASE's built-in ase.calculators.fleur is a stub that \"\n"
           "            \"raises and points here.)\") from error\n"
           "\n";
    if (!c.fleurRoot.empty())
        out << "os.environ[\"PATH\"] = (r\"" << c.fleurRoot << "\" + os.pathsep\n"
               "                      + os.environ.get(\"PATH\", \"\"))\n\n";
    out << "# inpgen builds the input from the structure; fleur runs the SCF.\n"
           "profile = FleurProfile(\n"
           "    command=os.environ.get(\"ASE_FLEUR_COMMAND\", \"fleur\"),\n"
           "    inpgen=os.environ.get(\"ASE_FLEUR_INPGEN\", \"inpgen\"),\n"
           ")\n"
           "\n"
           "atoms.calc = Fleur(\n"
           "    profile=profile,\n"
        << "    xc=\"" << c.fleurXc << "\",\n"
        // K_max is FLEUR's own interstitial cutoff, in bohr^-1 — a reciprocal
        // length, unlike WIEN2k's dimensionless RKmax. They are not
        // interchangeable and the comment says so where it will be read.
        << "    kmax=" << c.fleurKmax
        << ",  # bohr^-1 — interstitial plane-wave cutoff\n"
        << "    kpts=(" << c.kpts[0] << ", " << c.kpts[1] << ", " << c.kpts[2]
        << "),\n"
        << "    itmax=" << c.fleurMaxIterations << ",\n"
        << "    minDistance=" << c.fleurEnergyConvHtr << ",\n";
    if (c.spinPolarized)
        out << "    jspins=2,  # collinear spin polarization\n";
    out << ")\n";
    emitAseDrivenTasksOnly(out, "FLEUR");
}

// ---------------------------------------------------------------------------
// CP2K
// ---------------------------------------------------------------------------
void emitCp2k(std::ostringstream& out, const CalculatorConfig& c)
{
    out << "# --- CP2K ---------------------------------------------------------\n"
           "#\n"
           "# Gaussian and plane waves (GPW), through ase.calculators.cp2k.\n"
           "#\n"
           "# CP2K talks to ASE through a PERSISTENT `cp2k_shell` process rather\n"
           "# than one binary run per evaluation, which is what makes it fast\n"
           "# inside an MD or relaxation loop — the wavefunction is reused\n"
           "# between steps instead of being rebuilt from scratch.\n"
           "#\n"
           "# The two cutoffs below are NOT basis-set parameters, and this is\n"
           "# the single most common CP2K mistake:\n"
           "#\n"
           "#   basis_set  IS the basis. The wavefunctions are Gaussians, and\n"
           "#              DZVP -> TZVP is how the basis is improved.\n"
           "#   cutoff     is the plane-wave GRID the DENSITY is mapped onto.\n"
           "#   rel_cutoff decides how the multi-grid assigns each Gaussian to\n"
           "#              a grid level, and is what actually has to be\n"
           "#              converged alongside `cutoff`.\n"
           "#\n"
           "# Raising `cutoff` therefore refines a grid while the basis stays\n"
           "# exactly as small — the energy keeps moving and never converges to\n"
           "# anything the basis can represent.\n"
           "import os\n"
           "from ase.calculators.cp2k import CP2K\n"
           "\n"
        << "CP2K.command = os.environ.get(\"ASE_CP2K_COMMAND\", \""
        << (c.cp2kCommand.empty() ? std::string("cp2k_shell") : c.cp2kCommand)
        << "\")\n"
           "\n"
           "atoms.calc = CP2K(\n"
        << "    xc=\"" << c.cp2kXc << "\",\n"
        << "    cutoff=" << c.cp2kCutoffEv << ",  # eV — the DENSITY grid ("
        << c.cp2kCutoffEv * kRydbergPerEv << " Ry)\n"
        << "    basis_set=\"" << c.cp2kBasisSet << "\",\n"
        << "    basis_set_file=\"" << c.cp2kBasisSetFile << "\",\n"
        << "    pseudo_potential=\"" << c.cp2kPseudoPotential << "\",\n"
        << "    potential_file=\"" << c.cp2kPotentialFile << "\",\n"
        << "    max_scf=" << c.cp2kMaxScf << ",\n"
        << "    charge=" << c.charge << ",\n"
        << "    uks=" << (c.spinPolarized ? "True" : "False")
        << ",  # unrestricted Kohn-Sham (spin polarization)\n";
    if (c.multiplicity != 1)
        out << "    multiplicity=" << c.multiplicity << ",\n";
    // Stress is what a variable-cell relaxation needs, and CP2K only computes
    // it when asked — an omitted STRESS_TENSOR section makes the filter see
    // zeros and leave the cell exactly where it started.
    out << "    stress_tensor=" << (c.relaxCell ? "True" : "False") << ",\n";
    out << "    inp=\"\"\"&FORCE_EVAL\n"
           "  &DFT\n"
           "    &MGRID\n"
        << "      REL_CUTOFF [eV] " << c.cp2kRelCutoffEv << "\n"
           "    &END MGRID\n"
           "  &END DFT\n"
           "&END FORCE_EVAL\n";
    for (const std::string& entry : extraLines(c.cp2kExtraInput))
        out << entry << "\n";
    out << "\"\"\",\n"
           ")\n";
    emitAseDrivenTasksOnly(out, "CP2K");
}

// ---------------------------------------------------------------------------
// Amber
// ---------------------------------------------------------------------------
void emitAmber(std::ostringstream& out, const CalculatorConfig& c)
{
    out << "# --- Amber --------------------------------------------------------\n"
           "#\n"
           "# Classical biomolecular mechanics, through ase.calculators.amber,\n"
           "# which drives `sander`.\n"
           "#\n"
           "# Amber is an ENGINE, exactly like LAMMPS and GROMACS: the physics\n"
           "# lives entirely in the PRMTOP topology, which carries the atom\n"
           "# types, the charges and every bonded term. Nothing here can build\n"
           "# one — that is tleap's and antechamber's job — so a structure\n"
           "# without a matching prmtop cannot run, and the topology and the\n"
           "# coordinates must describe the SAME atoms in the SAME order.\n"
           "from ase.calculators.amber import Amber\n"
           "\n";
    if (c.amberTopologyFile.empty())
        out << "# EDIT ME: no prmtop was given. Build one with tleap:\n"
               "#     tleap -f leaprc.protein.ff19SB\n"
               "#     > mol = loadpdb system.pdb\n"
               "#     > saveamberparm mol system.prmtop system.inpcrd\n"
               "raise RuntimeError(\n"
               "    \"Amber needs a prmtop topology file — there is no force \"\n"
               "    \"field without one.\")\n\n";
    // sander's mdin decides what the binary DOES, and its default is a
    // minimization. imin=0 / nstlim=0 makes it the single force evaluation ASE
    // expects, so the optimizer above takes the steps rather than sander
    // silently relaxing before every reported energy.
    if (c.amberInputFile.empty())
        out << "# The mdin control file. Written here rather than required from\n"
               "# the user because sander's own default is a MINIMIZATION: with\n"
               "# imin=1 every \"force evaluation\" ASE asks for would be a\n"
               "# complete relaxation, and the trajectory would be a sequence of\n"
               "# already-relaxed structures. imin=0 with nstlim=0 is the single\n"
               "# energy+force evaluation an ASE calculator is supposed to be.\n"
               "with open(\"mm.in\", \"w\") as handle:\n"
               "    handle.write(\n"
               "        \"Single-point energy and forces for ASE\\n\"\n"
               "        \" &cntrl\\n\"\n"
               "        \"  imin=0, nstlim=0, ntx=1, irest=0,\\n\"\n"
               "        \"  ntb=1, cut=10.0, ntpr=1,\\n\"\n"
               "        \" &end\\n\")\n"
               "\n";
    out << "atoms.calc = Amber(\n"
        << "    amber_exe=\"" << c.amberExecutable << "\",\n"
        << "    infile=\""
        << (c.amberInputFile.empty() ? std::string("mm.in") : c.amberInputFile)
        << "\",\n"
           "    outfile=\"mm.out\",\n"
        << "    topologyfile=r\""
        << (c.amberTopologyFile.empty() ? std::string("system.prmtop")
                                        : c.amberTopologyFile)
        << "\",\n"
           "    incoordfile=\"mm.crd\",\n"
           ")\n"
           "# The calculator reads its coordinates from incoordfile, so the\n"
           "# staged geometry has to be written there before the first call.\n"
           "atoms.calc.write_coordinates(atoms, \"mm.crd\")\n";
    emitAseDrivenTasksOnly(out, "Amber");
}

} // namespace EngineBlocks

} // namespace calango::core
