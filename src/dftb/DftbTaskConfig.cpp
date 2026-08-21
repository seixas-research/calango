#include "dftb/DftbTaskConfig.hpp"

#include "core/LocaleSafeNumber.hpp"

#include <fstream>
#include <sstream>

namespace calango::dftb {

namespace {

std::vector<std::string> tokenize(const std::string& line)
{
    std::vector<std::string> tokens;
    std::istringstream stream(line);
    std::string token;
    while (stream >> token)
        tokens.push_back(token);
    return tokens;
}

bool parseBool(const std::string& text)
{
    return text == "true" || text == "1" || text == "yes" || text == "on";
}

} // namespace

dft::Outcome parseDftbTaskConfig(const std::string& text, DftbTaskConfig& out)
{
    out = DftbTaskConfig{};
    std::istringstream stream(text);
    std::string line;
    bool sawTask = false, sawStructure = false, sawSkDir = false,
         sawOutput = false;
    while (std::getline(stream, line)) {
        const auto hash = line.find('#');
        if (hash != std::string::npos)
            line = line.substr(0, hash);
        const auto tokens = tokenize(line);
        if (tokens.empty())
            continue;
        const std::string& key = tokens[0];
        const std::string value = tokens.size() > 1 ? tokens[1] : std::string();

        if (key == "task") {
            sawTask = true;
            if (value == "singlepoint") out.task = DftbTask::SinglePoint;
            else if (value == "bands") out.task = DftbTask::Bands;
            else if (value == "pdos") out.task = DftbTask::Pdos;
            else if (value == "unfolding") out.task = DftbTask::Unfolding;
            else if (value == "optics") out.task = DftbTask::Optics;
            else return dft::Outcome::invalid("unknown task: " + value);
        } else if (key == "structure") {
            out.structurePath = value;
            sawStructure = true;
        } else if (key == "skdir") {
            out.skDirectory = value;
            sawSkDir = true;
        } else if (key == "scc") {
            out.scc = parseBool(value);
        } else if (key == "scctol") {
            core::localeSafeParse(value, &out.sccToleranceElectrons);
        } else if (key == "maxsccsteps") {
            out.maxSccIterations = std::atoi(value.c_str());
        } else if (key == "fillingtemp") {
            core::localeSafeParse(value, &out.fillingTemperatureHartree);
        } else if (key == "mixing") {
            core::localeSafeParse(value, &out.mixingParameter);
        } else if (key == "anderson") {
            out.andersonMixing = parseBool(value);
        } else if (key == "kmesh") {
            if (tokens.size() < 4)
                return dft::Outcome::invalid("kmesh needs 3 integers");
            out.kMesh = {std::atoi(tokens[1].c_str()),
                         std::atoi(tokens[2].c_str()),
                         std::atoi(tokens[3].c_str())};
        } else if (key == "kpathfile") {
            out.kPathFile = value;
        } else if (key == "bandsbelow") {
            out.bandsBelow = std::atoi(value.c_str());
        } else if (key == "bandsabove") {
            out.bandsAbove = std::atoi(value.c_str());
        } else if (key == "pdosbroadening") {
            core::localeSafeParse(value, &out.pdosBroadeningHartree);
        } else if (key == "pdosbinwidth") {
            core::localeSafeParse(value, &out.pdosBinWidthHartree);
        } else if (key == "primitivestructure") {
            out.primitiveStructurePath = value;
        } else if (key == "unfoldingenergymin") {
            core::localeSafeParse(value, &out.unfoldingEnergyMinEv);
        } else if (key == "unfoldingenergymax") {
            core::localeSafeParse(value, &out.unfoldingEnergyMaxEv);
        } else if (key == "unfoldingenergybins") {
            out.unfoldingEnergyBins = std::atoi(value.c_str());
        } else if (key == "unfoldingsigma") {
            core::localeSafeParse(value, &out.unfoldingSigmaEv);
        } else if (key == "unfoldingweightthreshold") {
            core::localeSafeParse(value, &out.unfoldingWeightThreshold);
        } else if (key == "opticsomegamax") {
            core::localeSafeParse(value, &out.opticsOmegaMaxEv);
        } else if (key == "opticssteps") {
            out.opticsSteps = std::atoi(value.c_str());
        } else if (key == "opticsbroadening") {
            core::localeSafeParse(value, &out.opticsBroadeningEv);
        } else if (key == "opticsdirection") {
            out.opticsDirection = std::atoi(value.c_str());
        } else if (key == "opticsvacuumthickness") {
            core::localeSafeParse(value, &out.opticsVacuumThicknessAngstrom);
        } else if (key == "output") {
            out.outputPath = value;
            sawOutput = true;
        } else {
            out.unknownKeys.emplace_back(key, value);
        }
    }

    if (!sawTask)
        return dft::Outcome::invalid("manifest is missing 'task'");
    if (!sawStructure)
        return dft::Outcome::invalid("manifest is missing 'structure'");
    if (!sawSkDir)
        return dft::Outcome::invalid("manifest is missing 'skdir'");
    if (!sawOutput)
        return dft::Outcome::invalid("manifest is missing 'output'");
    if (!out.unknownKeys.empty()) {
        std::string message = "unrecognized manifest key(s):";
        for (const auto& [key, value] : out.unknownKeys)
            message += " '" + key + "'";
        return dft::Outcome::invalid(message);
    }
    if (out.task == DftbTask::Bands && out.kPathFile.empty())
        return dft::Outcome::invalid(
            "task=bands requires 'kpathfile'");
    if (out.task == DftbTask::Unfolding) {
        if (out.kPathFile.empty())
            return dft::Outcome::invalid("task=unfolding requires 'kpathfile' "
                                          "(primitive k-points)");
        if (out.primitiveStructurePath.empty())
            return dft::Outcome::invalid("task=unfolding requires "
                                          "'primitivestructure'");
    }

    return dft::Outcome::success();
}

dft::Outcome loadDftbTaskConfig(const std::string& path, DftbTaskConfig& out)
{
    std::ifstream file(path);
    if (!file)
        return dft::Outcome::invalid("cannot open manifest: " + path);
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return parseDftbTaskConfig(buffer.str(), out);
}

} // namespace calango::dftb
