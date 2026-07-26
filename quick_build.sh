# 1. Configure
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
    -DPython3_EXECUTABLE=$PWD/.venv/bin/python \
    -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt

# 2. Build
cmake --build build -j 2
