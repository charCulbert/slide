cmake_minimum_required(VERSION 3.24)

if(NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
    if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        set(CMAKE_OSX_DEPLOYMENT_TARGET 15.0 CACHE STRING "Minimum iOS version")
    elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
        set(CMAKE_OSX_DEPLOYMENT_TARGET 10.15 CACHE STRING "Minimum macOS version")
    endif()
endif()

project(slide VERSION 1.0.0 LANGUAGES C CXX)

if(APPLE)
    enable_language(OBJCXX)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(CMAKE_SYSTEM_PROCESSOR MATCHES "^wasm")
    set(ANY_WASM_TOOLCHAIN TRUE)
endif()

# Dependencies are pinned submodules; cache paths can override them locally.
set(external "${CMAKE_CURRENT_SOURCE_DIR}/external")
set(CLAP_SDK_ROOT "${external}/clap" CACHE PATH "CLAP SDK")
set(CLAP_HELPERS_ROOT "${external}/clap-helpers" CACHE PATH "clap-helpers")
set(CLAP_WRAPPER_ROOT "${external}/clap-wrapper" CACHE PATH "clap-wrapper (charCulbert fork)")
set(CLAP_WRAPPER_CHOC_ROOT "${external}/choc" CACHE PATH "CHOC")
set(CHAR_CLAP_UTILS_ROOT "${external}/char-clap-utils" CACHE PATH "char-clap-utils")
set(CHARDSP_ROOT "${external}/chardsp" CACHE PATH "chardsp")
set(COMPOST_ROOT "${external}/compost" CACHE PATH "compost web components")
foreach(root IN ITEMS CLAP_SDK_ROOT CLAP_HELPERS_ROOT CLAP_WRAPPER_ROOT CLAP_WRAPPER_CHOC_ROOT
                      CHAR_CLAP_UTILS_ROOT CHARDSP_ROOT COMPOST_ROOT)
    if(NOT EXISTS "${${root}}/CMakeLists.txt" AND NOT EXISTS "${${root}}/package.json"
       AND NOT EXISTS "${${root}}/include/char_clap_utils/WebUI.h")
        message(FATAL_ERROR
            "${root} is empty or missing: ${${root}}\n"
            "Point ${root} at an existing checkout.")
    endif()
endforeach()

# Guarded so this file also works when this project is added as a subdirectory of a
# larger build that already provides these targets.
if(NOT TARGET clap)
    add_subdirectory("${CLAP_SDK_ROOT}" clap-sdk EXCLUDE_FROM_ALL)
endif()
if(NOT TARGET clap-helpers)
    add_subdirectory("${CLAP_HELPERS_ROOT}" clap-helpers EXCLUDE_FROM_ALL)
endif()
if(NOT TARGET clap-wrapper-extensions)
    add_subdirectory("${CLAP_WRAPPER_ROOT}" clap-wrapper EXCLUDE_FROM_ALL)
endif()
if(NOT TARGET chardsp)
    add_subdirectory("${CHARDSP_ROOT}" chardsp EXCLUDE_FROM_ALL)
endif()

set(PLUGIN_OUTPUT_NAME "Slide")
set(PLUGIN_ID "com.charlieculbert.slide")

include("${CHAR_CLAP_UTILS_ROOT}/cmake/CharClapWebUI.cmake")
char_clap_stage_web_ui(slide
    SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/ui"
    COMPOST_ROOT "${COMPOST_ROOT}"
    COMPONENTS button)

add_library(slide-impl STATIC Plugin.cpp)
target_include_directories(slide-impl PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}" "${CHAR_CLAP_UTILS_ROOT}/include")
target_link_libraries(slide-impl PUBLIC
    clap
    clap-helpers
    clap-wrapper-extensions
    chardsp::chardsp)
char_clap_add_native_webview(slide-impl "${CLAP_WRAPPER_CHOC_ROOT}")
add_custom_target(slide-licenses
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE" "${slide_RESOURCE_DIRECTORY}/LICENSE"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md" "${slide_RESOURCE_DIRECTORY}/THIRD_PARTY_NOTICES.md"
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${slide_RESOURCE_DIRECTORY}/THIRD_PARTY_LICENSES"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_LICENSES" "${slide_RESOURCE_DIRECTORY}/THIRD_PARTY_LICENSES"
    DEPENDS slide-resources
    VERBATIM)
add_dependencies(slide-impl slide-licenses)

if(ANY_WASM_TOOLCHAIN)
    set(formats WCLAP)
else()
    set(formats CLAP)
    if(APPLE AND CMAKE_GENERATOR STREQUAL "Xcode")
        list(APPEND formats AUV3)
    endif()
endif()

make_clapfirst_plugins(
    TARGET_NAME slide
    IMPL_TARGET slide-impl
    OUTPUT_NAME "${PLUGIN_OUTPUT_NAME}"
    ENTRY_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/Entry.cpp"
    RESOURCE_DIRECTORY "${slide_RESOURCE_DIRECTORY}"
    BUNDLE_IDENTIFIER "${PLUGIN_ID}"
    BUNDLE_VERSION "${PROJECT_VERSION}"
    PLUGIN_FORMATS ${formats}
    # D5: 64 stages x 2.1 s per channel is ~52 MB of delay memory at 48 kHz, so a
    # browser host must reserve a 64 MiB heap up front. The maximum leaves room for
    # higher sample rates, where the same lines cost proportionally more.
    WCLAP_MEMORY_MINIMUM 64MiB
    WCLAP_MEMORY_INITIAL 64MiB
    WCLAP_MEMORY_MAXIMUM 128MiB
    AUV3_BUILD_STANDALONE TRUE
    AUV2_MANUFACTURER_NAME "Charlie Culbert"
    AUV2_MANUFACTURER_CODE "ChAr"
    AUV2_SUBTYPE_CODE "Slid"
    AUV2_INSTRUMENT_TYPE "aufx"
    ASSET_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/artifacts"
    COPY_AFTER_BUILD FALSE)

# The wrapper copies resources after linking; UI-only edits must trigger that step.
foreach(bundle IN ITEMS slide_clap slide_standalone slide_auv3)
    if(TARGET ${bundle})
        set_property(TARGET ${bundle} APPEND PROPERTY LINK_DEPENDS
            "${CMAKE_CURRENT_BINARY_DIR}/slide-resources.stamp")
    endif()
endforeach()

include(CTest)
if(BUILD_TESTING AND NOT ANY_WASM_TOOLCHAIN AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    add_executable(slide-tests tests/SlideTests.cpp)
    target_link_libraries(slide-tests PRIVATE slide-impl)
    # The laws fixture lives with the sources, not with the build.
    target_compile_definitions(slide-tests PRIVATE
        SLIDE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME slide-tests COMMAND slide-tests)
endif()
