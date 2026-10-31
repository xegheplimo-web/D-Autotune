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
# D2: deterministic tracking/hysteresis state tests (not accuracy metrics).
add_executable(d2_tracking test_d2_tracking.cpp corpus_gen.cpp
               ../app/src/main/cpp/PitchCorrector.cpp)
target_include_directories(d2_tracking PRIVATE ../app/src/main/cpp ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(d2_tracking PRIVATE cxx_std_17)
target_compile_options(d2_tracking PRIVATE -O2 -Wall -Wextra -Wpedantic)
add_test(NAME d2_tracking COMMAND d2_tracking)
# Same trick: ctest hides passing output, so the D2 table is re-run at build.
add_custom_command(TARGET d2_tracking POST_BUILD
                   COMMAND d2_tracking
                   COMMENT "D2 tracking + hysteresis test report")
# D3: deterministic TD-PSOLA shifter tests (output F0, formants, clicks,
# transients, signal delay, realtime safety).
add_executable(d3_shifter test_d3_shifter.cpp corpus_gen.cpp
               ../app/src/main/cpp/PitchCorrector.cpp)
target_include_directories(d3_shifter PRIVATE ../app/src/main/cpp ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(d3_shifter PRIVATE cxx_std_17)
target_compile_options(d3_shifter PRIVATE -O2 -Wall -Wextra -Wpedantic)
add_test(NAME d3_shifter COMMAND d3_shifter)
# Same trick: ctest hides passing output, so the D3 table is re-run at build.
add_custom_command(TARGET d3_shifter POST_BUILD
                   COMMAND d3_shifter
                   COMMENT "D3 TD-PSOLA shifter test report")
# D4: end-to-end full-chain verification — live TD-PSOLA vs frozen D2-era
# granular shifter (d2_reference) under identical conditions, both rates.
add_executable(d4_e2e test_d4_e2e.cpp corpus_gen.cpp metrics.cpp
               d2_reference.cpp ../app/src/main/cpp/PitchCorrector.cpp)
target_include_directories(d4_e2e PRIVATE ../app/src/main/cpp ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(d4_e2e PRIVATE cxx_std_17)
target_compile_options(d4_e2e PRIVATE -O2 -Wall -Wextra -Wpedantic)
add_test(NAME d4_e2e COMMAND d4_e2e)
# Same trick: ctest hides passing output, so the D4 A/B tables re-run at build.
add_custom_command(TARGET d4_e2e POST_BUILD
                   COMMAND d4_e2e
                   COMMENT "D4 end-to-end A/B verification report")
# DC-003 REV2: normalized cross-correlation lag detection (pure math, no Oboe).
add_executable(test_ping_math test_ping_math.cpp
               ../app/src/main/cpp/PingMath.cpp)
target_include_directories(test_ping_math PRIVATE ../app/src/main/cpp)
target_compile_features(test_ping_math PRIVATE cxx_std_17)
target_compile_options(test_ping_math PRIVATE -O2 -Wall -Wextra -Wpedantic)
add_test(NAME ping_math COMMAND test_ping_math)
# D5: real-voice validation harness (card G2-D5, GPT round-4 D-015).
# d5_harness = machinery unit tests on synthetic fixtures (always runs).
# d5_realvoice = R1-R5 batteries on docs/corpus/manifest.jsonl; SKIP-loud
# when the corpus is absent (corpus lands after the harness, orchestrator).
# d5_listening_pack = randomized A/B WAV pack + committed key for the
# BLOCKED_HUMAN 3-person blind test; same skip discipline.
set(D5_CORPUS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../docs/corpus")
add_executable(d5_harness test_d5_harness.cpp corpus_loader.cpp sha256.cpp
               corpus_gen.cpp metrics.cpp ../app/src/main/cpp/PitchCorrector.cpp)
target_include_directories(d5_harness PRIVATE ../app/src/main/cpp ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(d5_harness PRIVATE cxx_std_17)
target_compile_options(d5_harness PRIVATE -O2 -Wall -Wextra -Wpedantic)
add_test(NAME d5_harness COMMAND d5_harness)
add_executable(d5_realvoice test_d5_realvoice.cpp corpus_loader.cpp sha256.cpp
               corpus_gen.cpp metrics.cpp ../app/src/main/cpp/PitchCorrector.cpp)
target_include_directories(d5_realvoice PRIVATE ../app/src/main/cpp ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(d5_realvoice PRIVATE cxx_std_17)
target_compile_options(d5_realvoice PRIVATE -O2 -Wall -Wextra -Wpedantic)
target_compile_definitions(d5_realvoice PRIVATE D5_CORPUS_DIR="${D5_CORPUS_DIR}")
add_test(NAME d5_realvoice COMMAND d5_realvoice)
# Same trick as d0: ctest hides passing output; the SKIP/real table must
# reach the CI build log for docs/baseline/D5_CHANGES.md.
add_custom_command(TARGET d5_realvoice POST_BUILD
                   COMMAND d5_realvoice
                   COMMENT "D5 real-voice batteries report (SKIP until corpus lands)")
add_executable(d5_listening_pack d5_listening_pack.cpp corpus_loader.cpp sha256.cpp
               corpus_gen.cpp metrics.cpp ../app/src/main/cpp/PitchCorrector.cpp)
target_include_directories(d5_listening_pack PRIVATE ../app/src/main/cpp ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(d5_listening_pack PRIVATE cxx_std_17)
target_compile_options(d5_listening_pack PRIVATE -O2 -Wall -Wextra -Wpedantic)
target_compile_definitions(d5_listening_pack PRIVATE D5_CORPUS_DIR="${D5_CORPUS_DIR}")
add_test(NAME d5_listening_pack COMMAND d5_listening_pack)

