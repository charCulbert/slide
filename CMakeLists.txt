cmake_minimum_required(VERSION 3.24)

if(NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
    if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        set(CMAKE_OSX_DEPLOYMENT_TARGET 15.0 CACHE STRING "Minimum iOS version")
    elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
        set(CMAKE_OSX_DEPLOYMENT_TARGET 10.15 CACHE STRING "Minimum macOS version")
    endif()
endif()

project(tide VERSION 1.0.0 LANGUAGES C CXX)

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

# Dependencies are pinned submodules; cache paths can override them locally so
# a sibling checkout can be substituted while working on it, e.g.
#   cmake -S . -B build -DCLAP_WRAPPER_ROOT=../../clap-wrapper
set(external "${CMAKE_CURRENT_SOURCE_DIR}/external")
set(CLAP_SDK_ROOT "${external}/clap" CACHE PATH "CLAP SDK")
set(CLAP_HELPERS_ROOT "${external}/clap-helpers" CACHE PATH "clap-helpers")
set(CLAP_WRAPPER_ROOT "${external}/clap-wrapper" CACHE PATH "clap-wrapper (charCulbert fork)")
set(CLAP_WRAPPER_CHOC_ROOT "${external}/choc" CACHE PATH "CHOC")
set(CHAR_CLAP_UTILS_ROOT "${external}/char-clap-utils" CACHE PATH "char-clap-utils")
set(COMPOST_ROOT "${external}/compost" CACHE PATH "compost web components")
foreach(root IN ITEMS CLAP_SDK_ROOT CLAP_HELPERS_ROOT CLAP_WRAPPER_ROOT CLAP_WRAPPER_CHOC_ROOT
                      CHAR_CLAP_UTILS_ROOT COMPOST_ROOT)
    if(NOT EXISTS "${${root}}/CMakeLists.txt" AND NOT EXISTS "${${root}}/package.json"
       AND NOT EXISTS "${${root}}/include/char_clap_utils/WebUI.h")
        message(FATAL_ERROR
            "${root} is empty or missing: ${${root}}\n"
            "Run `git submodule update --init` or point ${root} at a checkout.")
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

set(PLUGIN_OUTPUT_NAME "Tide")
set(PLUGIN_ID "com.charlieculbert.tide")

include("${CHAR_CLAP_UTILS_ROOT}/cmake/CharClapWebUI.cmake")
char_clap_stage_web_ui(tide
    SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/ui"
    COMPOST_ROOT "${COMPOST_ROOT}"
    COMPONENTS knob)

add_library(tide-impl STATIC Plugin.cpp)
target_include_directories(tide-impl PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}" "${CHAR_CLAP_UTILS_ROOT}/include")
target_link_libraries(tide-impl PUBLIC
    clap
    clap-helpers
    clap-wrapper-extensions)
char_clap_add_native_webview(tide-impl "${CLAP_WRAPPER_CHOC_ROOT}")
add_custom_target(tide-licenses
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE" "${tide_RESOURCE_DIRECTORY}/LICENSE"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md" "${tide_RESOURCE_DIRECTORY}/THIRD_PARTY_NOTICES.md"
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${tide_RESOURCE_DIRECTORY}/THIRD_PARTY_LICENSES"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_LICENSES" "${tide_RESOURCE_DIRECTORY}/THIRD_PARTY_LICENSES"
    DEPENDS tide-resources
    VERBATIM)
add_dependencies(tide-impl tide-licenses)

if(ANY_WASM_TOOLCHAIN)
    set(formats WCLAP)
else()
    set(formats CLAP)
    if(APPLE AND CMAKE_GENERATOR STREQUAL "Xcode")
        list(APPEND formats AUV3)
    endif()
endif()

set(standalone)
if(APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS" AND NOT ANY_WASM_TOOLCHAIN)
    set(standalone STANDALONE_CONFIGURATIONS standalone "${PLUGIN_OUTPUT_NAME}" "${PLUGIN_ID}")
endif()

make_clapfirst_plugins(
    TARGET_NAME tide
    IMPL_TARGET tide-impl
    OUTPUT_NAME "${PLUGIN_OUTPUT_NAME}"
    ENTRY_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/Entry.cpp"
    RESOURCE_DIRECTORY "${tide_RESOURCE_DIRECTORY}"
    BUNDLE_IDENTIFIER "${PLUGIN_ID}"
    BUNDLE_VERSION "${PROJECT_VERSION}"
    PLUGIN_FORMATS ${formats}
    ${standalone}
    # No WCLAP_MEMORY_MINIMUM/INITIAL/MAXIMUM: the delay memory is allocated on
    # activate from the sample rate, so a browser host's default reservation is
    # enough and no memory.json is packaged.
    AUV3_BUILD_STANDALONE TRUE
    AUV2_MANUFACTURER_NAME "Charlie Culbert"
    AUV2_MANUFACTURER_CODE "ChAr"
    AUV2_SUBTYPE_CODE "Tide"
    AUV2_INSTRUMENT_TYPE "aufx"
    ASSET_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/artifacts"
    COPY_AFTER_BUILD FALSE)

if(TARGET tide_auv3_standalone)
    add_custom_command(TARGET tide_auv3_standalone POST_BUILD
        COMMAND /usr/bin/codesign --force --sign -
            "$<TARGET_BUNDLE_DIR:tide_auv3_standalone>"
        VERBATIM)
endif()

# The wrapper copies resources after linking; UI-only edits must trigger that step.
foreach(bundle IN ITEMS tide_clap tide_standalone tide_auv3)
    if(TARGET ${bundle})
        set_property(TARGET ${bundle} APPEND PROPERTY LINK_DEPENDS
            "${CMAKE_CURRENT_BINARY_DIR}/tide-resources.stamp")
    endif()
endforeach()

if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    add_executable(tide_ios_host Entry.cpp)
    target_link_libraries(tide_ios_host PRIVATE tide-impl)
    target_add_auv3_standalone_ios_wrapper(
        TARGET tide_ios_host
        OUTPUT_NAME "${PLUGIN_OUTPUT_NAME}"
        BUNDLE_IDENTIFIER "${PLUGIN_ID}"
        BUNDLE_VERSION "${PROJECT_VERSION}"
        AUV3_TARGET tide_auv3
        RESOURCE_DIRECTORY "${tide_RESOURCE_DIRECTORY}"
        AU_TYPE "aufx"
        AU_SUBTYPE "Tide"
        AU_MANUFACTURER "ChAr")
    set_target_properties(tide_ios_host PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/artifacts")
    add_dependencies(tide_all tide_ios_host)
endif()

include(CTest)
if(BUILD_TESTING AND NOT ANY_WASM_TOOLCHAIN AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    add_executable(tide-tests tests/TideTests.cpp)
    target_link_libraries(tide-tests PRIVATE tide-impl)
    add_test(NAME tide-tests COMMAND tide-tests)
endif()
