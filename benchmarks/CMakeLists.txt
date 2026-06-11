include(FetchContent)

if (NOT TARGET benchmark::benchmark)
  set(BENCHMARK_ENABLE_TESTING OFF CACHE INTERNAL "")
  set(BENCHMARK_ENABLE_INSTALL OFF CACHE INTERNAL "")
  set(BENCHMARK_INSTALL_DOCS OFF CACHE INTERNAL "")
  FetchContent_Declare(googlebenchmark
    URL https://github.com/google/benchmark/archive/refs/tags/v1.9.1.tar.gz
    URL_HASH SHA256=32131c08ee31eeff2c8968d7e874f3cb648034377dfc32a4c377fa8796d84981
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    EXCLUDE_FROM_ALL
  )
  FetchContent_MakeAvailable(googlebenchmark)
endif ()

set(AURORA_DIR ${CMAKE_SOURCE_DIR}/extern/aurora)

# Compiles aurora's GX encoder + FIFO + command processor + shader_info
# directly (same approach as aurora/tests/gx_fifo_tests) with arena-backed
# renderer stubs, so the per-frame CPU cost of GX command processing can be
# measured in isolation.
add_executable(gx_bench
  gx_bench.cpp
  gx_bench_stubs.cpp
  # GX API implementations (encoders)
  ${AURORA_DIR}/lib/dolphin/gx/GXBump.cpp
  ${AURORA_DIR}/lib/dolphin/gx/GXCull.cpp
  ${AURORA_DIR}/lib/dolphin/gx/GXCpu2Efb.cpp
  ${AURORA_DIR}/lib/dolphin/gx/GXDispList.cpp
  ${AURORA_DIR}/lib/dolphin/gx/GXDraw.cpp
  ${AURORA_DIR}/lib/dolphin/gx/GXExtra.cpp
  ${AURORA_DIR}/lib/dolphin/gx/GXFifo.cpp
  ${AURORA_DIR}/lib/dolphin/gx/GXFrameBuffer.cpp
  ${AURORA_DIR}/lib/dolphin/gx/GXGeometry.cpp
  ${AURORA_DIR}/lib/dolphin/gx/GXGet.cpp
  ${AURORA_DIR}/lib/dolphin/gx/GXLighting.cpp
  ${AURORA_DIR}/lib/dolphin/gx/GXManage.cpp
  ${AURORA_DIR}/lib/dolphin/gx/GXAurora.cpp
  ${AURORA_DIR}/lib/dolphin/gx/GXPerf.cpp
  ${AURORA_DIR}/lib/dolphin/gx/GXPixel.cpp
  ${AURORA_DIR}/lib/dolphin/gx/GXTev.cpp
  ${AURORA_DIR}/lib/dolphin/gx/GXTexture.cpp
  ${AURORA_DIR}/lib/dolphin/gx/GXTransform.cpp
  ${AURORA_DIR}/lib/dolphin/gx/GXVert.cpp
  # FIFO/command processor (encoder + decoder)
  ${AURORA_DIR}/lib/gx/fifo.cpp
  ${AURORA_DIR}/lib/gx/command_processor.cpp
  # Real per-draw shader info / uniform build
  ${AURORA_DIR}/lib/gx/shader_info.cpp
)

target_include_directories(gx_bench PRIVATE
  ${AURORA_DIR}/include
  ${AURORA_DIR}/lib
  ${AURORA_DIR}/lib/dolphin/gx
)

target_compile_definitions(gx_bench PRIVATE AURORA TARGET_PC)

target_link_libraries(gx_bench PRIVATE
  benchmark::benchmark
  fmt::fmt
  xxhash
  absl::flat_hash_map
  absl::btree
  dawn::dawncpp_headers
  TracyClient
  ${AURORA_SDL3_TARGET}
)
