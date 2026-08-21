#include "dftb/SlaterKosterTable.hpp"

#include "core/Element.hpp"

#include <algorithm>
#include <filesystem>

namespace calango::dftb {

namespace fs = std::filesystem;

Outcome SlaterKosterTable::load(const std::string& directory,
                                 const std::vector<int>& atomicNumbers)
{
    directory_ = directory;
    files_.clear();
    missing_.clear();

    if (directory.empty())
        return Outcome::invalid(
            "no Slater-Koster directory configured — point it at a "
            "downloaded parameter set (e.g. mio-1-1 or 3ob from dftb.org)");
    std::error_code ec;
    if (!fs::is_directory(directory, ec))
        return Outcome::invalid(
            "Slater-Koster directory does not exist: " + directory);

    std::vector<int> unique = atomicNumbers;
    std::sort(unique.begin(), unique.end());
    unique.erase(std::unique(unique.begin(), unique.end()), unique.end());

    for (int z1 : unique) {
        for (int z2 : unique) {
            const std::string sym1 = core::Elements::data(z1).symbol;
            const std::string sym2 = core::Elements::data(z2).symbol;
            const std::string fileName = sym1 + "-" + sym2 + ".skf";
            const fs::path path = fs::path(directory) / fileName;

            SlaterKosterFile parsed;
            const Outcome outcome = loadSlaterKosterFile(path.string(), parsed);
            if (!outcome.ok()) {
                missing_.push_back({fileName, outcome.message});
                continue;
            }
            files_[{z1, z2}] = std::move(parsed);
        }
    }

    if (!missing_.empty()) {
        std::string message =
            "missing or unreadable Slater-Koster file(s) in " + directory
            + ":";
        for (const auto& m : missing_)
            message += " " + m.fileName + " (" + m.reason + ");";
        return Outcome::invalid(message);
    }
    return Outcome::success();
}

const SlaterKosterFile* SlaterKosterTable::pair(int atomicNumberFirst,
                                                  int atomicNumberSecond) const
{
    const auto it = files_.find({atomicNumberFirst, atomicNumberSecond});
    return it == files_.end() ? nullptr : &it->second;
}

const std::array<OnsiteShell, 3>*
SlaterKosterTable::onsite(int atomicNumber) const
{
    const SlaterKosterFile* file = pair(atomicNumber, atomicNumber);
    if (!file || !file->homonuclear)
        return nullptr;
    return &file->onsite;
}

bool SlaterKosterTable::hasPShell(int atomicNumber) const
{
    const auto* shells = onsite(atomicNumber);
    if (!shells)
        return false;
    return (*shells)[1].occupation > 0.0; // index 1 = p, see AngularMomentum
}

} // namespace calango::dftb
