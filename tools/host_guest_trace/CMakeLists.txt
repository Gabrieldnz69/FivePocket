cmake_minimum_required(VERSION 3.22)
project(PrivateHostGuestTrace LANGUAGES CXX)

if(NOT APPLE AND NOT ANDROID)
    message(FATAL_ERROR "The native trace harness supports macOS and Android diagnostics")
endif()

foreach(required_variable PPC_DIR BRIDGE_DIR REPOSITORY_ROOT EXPECTED_CHUNK_COUNT)
    if(NOT DEFINED ${required_variable})
        message(FATAL_ERROR "${required_variable} must be provided by run_trace.py")
    endif()
endforeach()

foreach(required_directory PPC_DIR BRIDGE_DIR REPOSITORY_ROOT)
    if(NOT IS_DIRECTORY "${${required_directory}}")
        message(FATAL_ERROR "${required_directory} is not a directory")
    endif()
endforeach()

file(GLOB PPC_CHUNKS CONFIGURE_DEPENDS "${PPC_DIR}/ppc_recomp.*.cpp")
list(SORT PPC_CHUNKS COMPARE NATURAL)
list(LENGTH PPC_CHUNKS PPC_CHUNK_COUNT)
if(NOT PPC_CHUNK_COUNT EQUAL EXPECTED_CHUNK_COUNT)
    message(FATAL_ERROR
        "Private chunk count changed after validation: expected ${EXPECTED_CHUNK_COUNT}, found ${PPC_CHUNK_COUNT}")
endif()

set(ANDROID_RUNTIME_DIR "${REPOSITORY_ROOT}/android/app/src/main/cpp")
set(SIMDE_DIR "${REPOSITORY_ROOT}/tools/XenonRecomp/thirdparty/simde")

# A Thor boot CPU sample identified per-pixel untile work as a hot path.
# Keep exact integer permutations and bounds checks; no fast-math change.
set_source_files_properties("${ANDROID_RUNTIME_DIR}/xenos_texture_2d.cpp"
    PROPERTIES COMPILE_OPTIONS "-O2")
# Remaining native dispatch/lookup costs were visible in the optimized Thor
# profile. Retain strict FP and defined wrapping without changing game behavior.
set_source_files_properties(indirect_call_resolver.cpp
    "${ANDROID_RUNTIME_DIR}/guest_import_registry.cpp"
    "${BRIDGE_DIR}/generated_guest_import_bridge.cpp"
    PROPERTIES COMPILE_OPTIONS "-O2;-fno-strict-aliasing;-ffp-model=strict;-fwrapv")
# The generated import adapter was previously optimized only by a private
# build command. Rebuilding it from Ninja after a registry header change must
# not silently fall back to the target-wide -O0 diagnostic default.

set(REQUIRED_PRIVATE_FILES
    "${PPC_DIR}/ppc_func_mapping.cpp"
    "${PPC_DIR}/ppc_context.h"
    "${PPC_DIR}/ppc_config.h"
    "${PPC_DIR}/ppc_recomp_shared.h"
    "${BRIDGE_DIR}/generated_guest_import_bridge.cpp"
    "${BRIDGE_DIR}/generated_guest_import_bridge.h"
    "${BRIDGE_DIR}/remaining_xbox_import_guarded_stops.cpp"
)
foreach(required_file IN LISTS REQUIRED_PRIVATE_FILES)
    if(NOT EXISTS "${required_file}" OR IS_SYMLINK "${required_file}")
        message(FATAL_ERROR "Required private input is missing or a symlink")
    endif()
endforeach()

include("${CMAKE_CURRENT_SOURCE_DIR}/stage_ppc.cmake")
set(PPC_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/validated-ppc")
stage_private_ppc("${PPC_DIR}" "${PPC_BUILD_DIR}" "${EXPECTED_CHUNK_COUNT}")

option(RECOMP_PRIVATE_TRACE_LIBRARY "Build a private one-shot Android trace embedding library" OFF)
if(RECOMP_PRIVATE_TRACE_LIBRARY)
    if(NOT ANDROID)
        message(FATAL_ERROR "Private trace embedding is currently Android-only")
    endif()
    add_library(host_guest_trace SHARED)
    target_compile_definitions(host_guest_trace PRIVATE RECOMP_PRIVATE_TRACE_LIBRARY=1)
    set_target_properties(host_guest_trace PROPERTIES
        CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES
        LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/private_trace.exports")
    target_link_options(host_guest_trace PRIVATE
        "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/private_trace.exports" "-Wl,--no-undefined")
else()
    add_executable(host_guest_trace)
endif()
target_sources(host_guest_trace PRIVATE
    trace_runner.cpp
    indirect_call_resolver.cpp
    ${STAGED_PPC_CHUNKS}
    "${PPC_BUILD_DIR}/ppc_func_mapping.cpp"
    "${BRIDGE_DIR}/generated_guest_import_bridge.cpp"
    "${BRIDGE_DIR}/remaining_xbox_import_guarded_stops.cpp"
    "${ANDROID_RUNTIME_DIR}/guest_address_space.cpp"
    "${ANDROID_RUNTIME_DIR}/guest_cooperative_executor.cpp"
    "${ANDROID_RUNTIME_DIR}/guest_execution_guard.cpp"
    "${ANDROID_RUNTIME_DIR}/guest_image_loader.cpp"
    "${ANDROID_RUNTIME_DIR}/guest_import_registry.cpp"
    "${ANDROID_RUNTIME_DIR}/guest_native_initialization.cpp"
    "${ANDROID_RUNTIME_DIR}/xenos_command_stream.cpp"
    "${ANDROID_RUNTIME_DIR}/xenos_texture_2d.cpp"
    "${ANDROID_RUNTIME_DIR}/guest_thread_environment.cpp"
    "${ANDROID_RUNTIME_DIR}/guest_thread_storage_pool.cpp"
    "${ANDROID_RUNTIME_DIR}/guest_timebase.cpp"
    "${ANDROID_RUNTIME_DIR}/guest_xex_header.cpp"
    "${ANDROID_RUNTIME_DIR}/guest_title_environment.cpp"
)

target_compile_features(host_guest_trace PRIVATE cxx_std_20)
set(NATIVE_XMA_FFMPEG_DIR "" CACHE PATH "Pinned generated Xenia FFmpeg Android build/header directory")
set(NATIVE_XMA_ARCHIVE_DIR "" CACHE PATH "Pinned Android XMA static archives")
if(NATIVE_XMA_FFMPEG_DIR)
    if(NOT ANDROID OR NOT EXISTS "${NATIVE_XMA_FFMPEG_DIR}/libavcodec/avcodec.h" OR
       NOT EXISTS "${NATIVE_XMA_ARCHIVE_DIR}/libavcodec.a" OR
       NOT EXISTS "${NATIVE_XMA_ARCHIVE_DIR}/libavutil.a")
        message(FATAL_ERROR "Native XMA requires built Android Xenia FFmpeg archives and matching headers")
    endif()
    target_compile_definitions(host_guest_trace PRIVATE RECOMP_NATIVE_XMA=1)
    target_include_directories(host_guest_trace PRIVATE "${NATIVE_XMA_FFMPEG_DIR}")
    target_sources(host_guest_trace PRIVATE
        "${ANDROID_RUNTIME_DIR}/native_xma_frame_decoder.cpp"
        "${ANDROID_RUNTIME_DIR}/native_xma_context_pool.cpp")
    set_source_files_properties(
        "${ANDROID_RUNTIME_DIR}/native_xma_frame_decoder.cpp"
        "${ANDROID_RUNTIME_DIR}/native_xma_context_pool.cpp" PROPERTIES COMPILE_OPTIONS "-O2")
    target_link_libraries(host_guest_trace PRIVATE
        "${NATIVE_XMA_ARCHIVE_DIR}/libavcodec.a" "${NATIVE_XMA_ARCHIVE_DIR}/libavutil.a" m log)
endif()
option(RECOMP_RENDERER_BOUNDARY_TRACE "Experimental GTA V API clear/draw/resolve with original-driver fallback and boundary tracing" OFF)
if(RECOMP_RENDERER_BOUNDARY_TRACE)
    target_sources(host_guest_trace PRIVATE renderer_boundary_trace.cpp)
    set_property(SOURCE trace_runner.cpp APPEND PROPERTY COMPILE_DEFINITIONS
        RECOMP_RENDERER_BOUNDARY_TRACE=1)
    if(ANDROID)
        target_link_libraries(host_guest_trace PRIVATE log)
    endif()
endif()
find_package(Threads REQUIRED)
target_link_libraries(host_guest_trace PRIVATE Threads::Threads)
if(ANDROID)
    target_link_libraries(host_guest_trace PRIVATE android)
endif()
if(RECOMP_PRIVATE_TRACE_LIBRARY)
    add_executable(private_trace_library_test tests/private_trace_library_test.cpp)
    target_compile_features(private_trace_library_test PRIVATE cxx_std_17)
    target_compile_options(private_trace_library_test PRIVATE -UNDEBUG -Wall -Wextra -Werror)
    target_link_libraries(private_trace_library_test PRIVATE dl Threads::Threads)
    add_executable(private_trace_library_v2_test tests/private_trace_library_v2_test.cpp)
    target_include_directories(private_trace_library_v2_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_features(private_trace_library_v2_test PRIVATE cxx_std_17)
    target_compile_options(private_trace_library_v2_test PRIVATE -UNDEBUG -Wall -Wextra -Werror)
    target_link_libraries(private_trace_library_v2_test PRIVATE dl Threads::Threads)
    add_executable(private_trace_library_v3_test tests/private_trace_library_v3_test.cpp)
    target_include_directories(private_trace_library_v3_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_features(private_trace_library_v3_test PRIVATE cxx_std_17)
    target_compile_options(private_trace_library_v3_test PRIVATE -UNDEBUG -Wall -Wextra -Werror)
    target_link_libraries(private_trace_library_v3_test PRIVATE dl Threads::Threads)
    add_executable(private_trace_library_v4_test tests/private_trace_library_v4_test.cpp)
    target_include_directories(private_trace_library_v4_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_features(private_trace_library_v4_test PRIVATE cxx_std_17)
    target_compile_options(private_trace_library_v4_test PRIVATE -UNDEBUG -Wall -Wextra -Werror)
    target_link_libraries(private_trace_library_v4_test PRIVATE dl Threads::Threads)
endif()
if(ANDROID)
    include("${REPOSITORY_ROOT}/tools/vulkan_probe/native_constant_shaders.cmake")
    attach_native_constant_shaders(host_guest_trace)
    target_sources(host_guest_trace PRIVATE
        "${ANDROID_RUNTIME_DIR}/aaudio_output_stream.cpp"
        "${ANDROID_RUNTIME_DIR}/vulkan_engine.cpp"
        "${ANDROID_RUNTIME_DIR}/native_texture_image.cpp"
        "${ANDROID_RUNTIME_DIR}/native_api_draw_target.cpp"
        "${REPOSITORY_ROOT}/tools/vulkan_probe/device.cpp")
    target_include_directories(host_guest_trace PRIVATE "${REPOSITORY_ROOT}/tools/vulkan_probe")
    target_link_libraries(host_guest_trace PRIVATE vulkan aaudio)
endif()
target_compile_definitions(host_guest_trace PRIVATE SIMDE_ENABLE_NATIVE_ALIASES)
target_compile_options(host_guest_trace PRIVATE
    -include "${CMAKE_CURRENT_SOURCE_DIR}/trace_prelude.h"
    -O0
    -g0
    -Wno-unused-variable
    -Wno-unused-but-set-variable
)
# These host paths run at every translated-function checkpoint or memory access.
# Optimize them without changing the generated PPC corpus, scheduling quantum,
# guard checks, floating-point policy or retained diagnostic limits.
set_source_files_properties(
    trace_runner.cpp
    "${ANDROID_RUNTIME_DIR}/guest_execution_guard.cpp"
    "${ANDROID_RUNTIME_DIR}/guest_cooperative_executor.cpp"
    PROPERTIES COMPILE_OPTIONS "-O2")
# Keep the large translated corpus's fast diagnostic build, but optimize native
# per-pixel conversion/packing and Vulkan submission code. No fast-math or
# changed guest execution/scheduler semantics.
if(ANDROID)
    set_source_files_properties(
        "${CMAKE_CURRENT_SOURCE_DIR}/renderer_boundary_trace.cpp"
        "${ANDROID_RUNTIME_DIR}/native_api_draw_target.cpp"
        PROPERTIES COMPILE_OPTIONS "-O2")
endif()
target_include_directories(host_guest_trace PRIVATE
    "${PPC_BUILD_DIR}"
    "${BRIDGE_DIR}"
    "${ANDROID_RUNTIME_DIR}"
    "${SIMDE_DIR}"
)

set_target_properties(host_guest_trace PROPERTIES
    CXX_EXTENSIONS OFF
)
