#include "core/DftbNativeScriptGenerator.hpp"

namespace calango::core {

void emitDftbNativeWrapper(std::ostringstream& out, const CalculatorConfig& c,
                            const std::string& task,
                            const std::vector<std::string>& extraManifestLines,
                            const std::string& outputFileName)
{
    out << "# Calango's own native SCC-DFTB engine — not an ASE calculator,\n"
        << "# so there is no atoms.calc to build here. This script exports\n"
        << "# the structure, writes a plain-text task manifest, and execs\n"
        << "# the native calango-dftb-run binary as a subprocess, relaying\n"
        << "# its stdout line for line (its CALANGO_* markers ARE this\n"
        << "# script's own markers, unmodified).\n"
        << "import subprocess\n"
        << "\n"
        << "write(\"structure.extxyz\", atoms)\n"
        << "\n"
        << "with open(\"dftb_manifest.txt\", \"w\") as _fh:\n"
        << "    _fh.write(\"task " << task << "\\n\")\n"
        << "    _fh.write(\"structure structure.extxyz\\n\")\n"
        << "    _fh.write(r\"skdir " << c.dftbSlakoDir << "\" + \"\\n\")\n"
        << "    _fh.write(\"scc " << (c.dftbScc ? "true" : "false") << "\\n\")\n"
        << "    _fh.write(\"scctol " << c.dftbSccTolerance << "\\n\")\n"
        << "    _fh.write(\"maxsccsteps " << c.dftbMaxSccIterations << "\\n\")\n"
        // K is what the wizard field collects; the manifest itself is in
        // Hartree (kB = 3.166811563e-6 Hartree/K), matching DftbTaskConfig's
        // own documented unit.
        << "    _fh.write(\"fillingtemp \" + repr("
        << c.dftbFillingTemperatureK << " * 3.166811563e-6) + \"\\n\")\n"
        << "    _fh.write(\"kmesh \" + \" \".join(str(v) for v in ("
        << c.kpts[0] << ", " << c.kpts[1] << ", " << c.kpts[2] << ")) + \"\\n\")\n";
    for (const auto& line : extraManifestLines)
        out << "    _fh.write(r\"" << line << "\" + \"\\n\")\n";
    out << "    _fh.write(\"output " << outputFileName << "\\n\")\n"
        << "\n"
        << "_proc = subprocess.Popen(\n"
        << "    [r\"" << c.dftbNativeBinaryPath << "\", \"dftb_manifest.txt\"],\n"
        << "    stdout=subprocess.PIPE, stderr=subprocess.STDOUT,\n"
        << "    text=True, bufsize=1,\n"
        << ")\n"
        << "for _line in _proc.stdout:\n"
        << "    print(_line, end=\"\", flush=True)\n"
        << "_rc = _proc.wait()\n"
        << "if _rc != 0:\n"
        << "    raise RuntimeError(\n"
        << "        f\"calango-dftb-run exited with code {_rc} — see the "
        << "output above\")\n";
}

} // namespace calango::core
