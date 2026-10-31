cmake_minimum_required(VERSION 3.20)
project(dautotune_host_tests LANGUAGES CXX)
add_executable(test_dsp test_dsp.cpp ../app/src/main/cpp/PitchCorrector.cpp)
target_include_directories(test_dsp PRIVATE ../app/src/main/cpp)
target_compile_features(test_dsp PRIVATE cxx_std_17)
target_compile_options(test_dsp PRIVATE -O2 -Wall -Wextra -Wpedantic)
enable_testing()
add_test(NAME pitch_corrector COMMAND test_dsp)
add_executable(bench_dsp bench_dsp.cpp ../app/src/main/cpp/PitchCorrector.cpp)
target_include_directories(bench_dsp PRIVATE ../app/src/main/cpp)
target_compile_features(bench_dsp PRIVATE cxx_std_17)
target_compile_options(bench_dsp PRIVATE -O2 -Wall -Wextra -Wpedantic)
add_executable(d0_baseline test_d0_baseline.cpp corpus_gen.cpp metrics.cpp
               ../app/src/main/cpp/PitchCorrector.cpp)
target_include_directories(d0_baseline PRIVATE ../app/src/main/cpp ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(d0_baseline PRIVATE cxx_std_17)
target_compile_options(d0_baseline PRIVATE -O2 -Wall -Wextra -Wpedantic)
add_test(NAME d0_baseline COMMAND d0_baseline)
# ctest only surfaces output on failure; re-run once during `cmake --build` so
# the baseline table lands in the CI build log for docs/baseline/D0_BASELINE.md.
add_custom_command(TARGET d0_baseline POST_BUILD
                   COMMAND d0_baseline
                   COMMENT "D0 baseline report")
# D1: frozen D0 reference detector + live A/B comparison on the same corpus.
add_executable(d1_improvement test_d1_improvement.cpp corpus_gen.cpp metrics.cpp
               d0_reference.cpp ../app/src/main/cpp/PitchCorrector.cpp)
target_include_directories(d1_improvement PRIVATE ../app/src/main/cpp ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(d1_improvement PRIVATE cxx_std_17)
target_compile_options(d1_improvement PRIVATE -O2 -Wall -Wextra -Wpedantic)
add_test(NAME d1_improvement COMMAND d1_improvement)
# Same trick as d0_baseline: the D0-vs-D1 table must reach the CI build log.
add_custom_command(TARGET d1_improvement POST_BUILD
                   COMMAND d1_improvement
                   COMMENT "D1 vs D0 improvement report")

