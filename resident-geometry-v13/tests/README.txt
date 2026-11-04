Test facoltativo del core CPU, da ~/mhfu-vita:

g++ -std=c++20 -O2 -ffp-contract=off -fno-fast-math -Iresident-geometry-v13 -Ivita/runtime resident-geometry-v13/tests/cache.cpp -o build/resident_geometry_test
build/resident_geometry_test

Non sostituisce la build ARM e la prova sulla Vita.
