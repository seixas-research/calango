#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace calango::core {

/// A NumPy `.npy` array read back into C++.
///
/// Exists because the electron-phonon matrix elements are produced by GPAW in
/// Python and consumed by the analysis here. JSON is the wrong carrier for
/// them: the array is (spins, q, k, modes, bands, bands) complex, which is
/// 1.3 MB for a small test case and tens of gigabytes for a production run —
/// text would multiply that and cost a parse of every digit. `.npy` is the
/// format GPAW already writes (`bloch_matrix(savetofile=True)`), so reading
/// it means no extra conversion step and no second copy on disk.
///
/// Deliberately a small reader, not a NumPy port: version 1.0 and 2.0
/// headers, C order, little-endian `<f8` and `<c16`. Anything else is
/// reported rather than guessed at, because a silently misread binary array
/// produces plausible numbers.
struct NumpyArray {
    enum class Type { Float64, Complex128 };

    Type type = Type::Float64;
    std::vector<std::size_t> shape;
    /// Real arrays: one entry per element. Complex arrays: the MAGNITUDE
    /// SQUARED of each element.
    ///
    /// |z|^2 rather than the complex value, because that is the only thing
    /// the electron-phonon sums want and keeping the phases would double the
    /// memory of the largest array in the pipeline for nothing.
    std::vector<double> values;

    std::size_t elementCount() const;
    /// Total elements implied by `shape`; disagreement with `values.size()`
    /// means a truncated file.
    std::size_t shapeProduct() const;
};

/// Read `path`. Returns false with `error` set on anything unsupported or
/// malformed — a wrong dtype, Fortran order, a truncated body.
bool readNumpyArray(const std::string& path, NumpyArray& out,
                    std::string* error);

} // namespace calango::core
