cmake_minimum_required(VERSION 3.15)
project(blunt_codec VERSION 1.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)

option(BLUNT_BUILD_TOOLS "Build CLI tools" ON)
option(BLUNT_BUILD_TESTS "Build tests" ON)

add_library(blunt STATIC
    src/blunt_decoder.c
    src/blunt_encoder.c
    src/blunt_tables.c
    src/blunt_simd.c
    src/blunt_audio.c
)

target_include_directories(blunt PUBLIC include)

if(MSVC)
    target_compile_options(blunt PRIVATE /W4 /O2 /arch:AVX2)
else()
    target_compile_options(blunt PRIVATE -Wall -Wextra -O2 -march=native)
endif()

target_link_libraries(blunt PUBLIC m)

if(BLUNT_BUILD_TOOLS)
    add_executable(blunt_decode tools/blunt_decode.c)
    target_link_libraries(blunt_decode PRIVATE blunt)

    add_executable(blunt_encode tools/blunt_encode.c)
    target_link_libraries(blunt_encode PRIVATE blunt)

    add_executable(blunt_info tools/blunt_info.c)
    target_link_libraries(blunt_info PRIVATE blunt)

    add_executable(blunt_wav tools/blunt_wav.c)
    target_link_libraries(blunt_wav PRIVATE blunt)
endif()

if(BLUNT_BUILD_TESTS)
    enable_testing()
    add_executable(test_roundtrip tests/test_roundtrip.c)
    target_link_libraries(test_roundtrip PRIVATE blunt m)
    add_test(NAME roundtrip COMMAND test_roundtrip)

    add_executable(debug_test tests/debug_test.c)
    target_link_libraries(debug_test PRIVATE blunt m)

    add_executable(debug_iframe tests/debug_iframe.c)
    target_link_libraries(debug_iframe PRIVATE blunt m)

    add_executable(debug_large tests/debug_large.c)
    target_link_libraries(debug_large PRIVATE blunt m)

    add_executable(debug_color tests/debug_color.c)
    target_link_libraries(debug_color PRIVATE blunt m)

    add_executable(debug_pf tests/debug_pf.c)
    target_link_libraries(debug_pf PRIVATE blunt m)

    add_executable(debug_ident tests/debug_ident.c)
    target_link_libraries(debug_ident PRIVATE blunt m)

    add_executable(debug_bar tests/debug_bar.c)
    target_link_libraries(debug_bar PRIVATE blunt m)

    add_executable(debug_1mb tests/debug_1mb.c)
    target_link_libraries(debug_1mb PRIVATE blunt m)

    add_executable(debug_quality tests/debug_quality.c)
    target_link_libraries(debug_quality PRIVATE blunt m)

    add_executable(debug_qfix tests/debug_qfix.c)
    target_link_libraries(debug_qfix PRIVATE blunt m)

    add_executable(debug_block tests/debug_block.c)
    target_link_libraries(debug_block PRIVATE blunt m)

    add_executable(debug_q16 tests/debug_q16.c)
    target_link_libraries(debug_q16 PRIVATE blunt m)

    add_executable(test_audio_roundtrip tests/test_audio_roundtrip.c)
    target_link_libraries(test_audio_roundtrip PRIVATE blunt m)

    add_executable(test_audio_unit tests/test_audio_unit.c)
    target_link_libraries(test_audio_unit PRIVATE blunt m)

    add_executable(test_audio_direct tests/test_audio_direct.c)
    target_link_libraries(test_audio_direct PRIVATE blunt m)

    add_executable(test_audio_stereo tests/test_audio_stereo.c)
    target_link_libraries(test_audio_stereo PRIVATE blunt m)

    add_executable(test_audio_debug tests/test_audio_debug.c)
    target_link_libraries(test_audio_debug PRIVATE blunt m)

    add_executable(test_audio_sizes tests/test_audio_sizes.c)
    target_link_libraries(test_audio_sizes PRIVATE blunt m)

    add_executable(test_audio_psnr tests/test_audio_psnr.c)
    target_link_libraries(test_audio_psnr PRIVATE blunt m)

    add_executable(test_audio_pipeline tests/test_audio_pipeline.c)
    target_link_libraries(test_audio_pipeline PRIVATE blunt m)

    add_executable(test_audio_diag tests/test_audio_diag.c)
    target_link_libraries(test_audio_diag PRIVATE blunt m)

    add_executable(test_nz_count tests/test_nz_count.c)
    target_link_libraries(test_nz_count PRIVATE blunt m)

    add_executable(test_bitstream_trace tests/test_bitstream_trace.c)
    target_link_libraries(test_bitstream_trace PRIVATE blunt m)

    add_executable(test_bs_dump tests/test_bs_dump.c)
    target_link_libraries(test_bs_dump PRIVATE blunt m)

    add_executable(test_ch1_trace tests/test_ch1_trace.c)
    target_link_libraries(test_ch1_trace PRIVATE blunt m)
endif()
