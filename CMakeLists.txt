cmake_minimum_required(VERSION 3.28)

if(CMAKE_HOST_APPLE AND NOT CMAKE_SYSTEM_NAME AND NOT DEFINED CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "iOS" AND NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "15.0")
endif()

project(Slide VERSION 1.0.0 LANGUAGES C CXX)
if(APPLE)
    enable_language(OBJC OBJCXX)
endif()
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(MSVC) # clap-wrapper and the VST3 SDK use the DLL runtime (/MD)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

set(LIBS "${CMAKE_CURRENT_SOURCE_DIR}/libs")
include(${LIBS}/core/core.cmake)
include(${LIBS}/webview/webview.cmake)

if(NOT EXISTS "${LIBS}/chardsp/chardsp/chardsp_OnePole.h")
    message(FATAL_ERROR "libs/chardsp is empty: run `git submodule update --init`")
endif()

add_library(Slide_impl STATIC plugin.cpp)
target_include_directories(Slide_impl PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/dsp" "${LIBS}/chardsp")
core_add_to(Slide_impl)
webview_add_to(Slide_impl)

webview_resources(resources Slide resources)

set(identity
    OUTPUT_NAME "Slide"
    BUNDLE_IDENTIFIER "com.charlieculbert.slide"
    BUNDLE_VERSION "${PROJECT_VERSION}")

if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    add_executable(Slide_auv3 entry.cpp)
    target_link_libraries(Slide_auv3 PRIVATE Slide_impl)
    target_add_auv3_wrapper(TARGET Slide_auv3 ${identity}
        MANUFACTURER_NAME "Charlie Culbert" MANUFACTURER_CODE "ChAr"
        SUBTYPE_CODE "Slid" INSTRUMENT_TYPE "aufx")
    add_executable(Slide_ios entry.cpp)
    target_link_libraries(Slide_ios PRIVATE Slide_impl)
    target_add_auv3_standalone_ios_wrapper(TARGET Slide_ios
        OUTPUT_NAME "Slide" BUNDLE_IDENTIFIER "com.charlieculbert.slide.ios"
        BUNDLE_VERSION "${PROJECT_VERSION}" AUV3_TARGET Slide_auv3
        AU_TYPE "aufx" AU_SUBTYPE "Slid" AU_MANUFACTURER "ChAr")
    return()
endif()

set(formats CLAP VST3)
if(APPLE)
    list(APPEND formats AUV2)
    if(CMAKE_GENERATOR STREQUAL "Xcode")
        list(APPEND formats AUV3)
    endif()
endif()
if(AAX_SDK_ROOT)
    if(NOT EXISTS "${AAX_SDK_ROOT}/Interfaces/AAX.h")
        message(FATAL_ERROR "AAX_SDK_ROOT (${AAX_SDK_ROOT}) is not an AAX SDK")
    endif()
    list(APPEND formats AAX)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "WASI")
    set(formats WCLAP)
endif()

make_clapfirst_plugins(
    TARGET_NAME Slide
    IMPL_TARGET Slide_impl
    ENTRY_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/entry.cpp"
    ${identity}
    AUV2_MANUFACTURER_NAME "Charlie Culbert"
    AUV2_MANUFACTURER_CODE "ChAr"
    AUV2_SUBTYPE_CODE "Slid"
    AUV2_INSTRUMENT_TYPE "aufx"
    PLUGIN_FORMATS ${formats}
    WINDOWS_FOLDER_VST3 TRUE
    STANDALONE_CONFIGURATIONS standalone "Slide" "com.charlieculbert.slide"
    COPY_AFTER_BUILD FALSE)

if(TARGET Slide_aax AND APPLE)
    set_target_properties(Slide_aax PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
endif()

core_ship_resources(Slide "${resources}" DEPENDS Slide_resources)


enable_testing()
if(NOT CMAKE_SYSTEM_NAME STREQUAL "WASI")
    add_executable(slide-tests tests/SlideTests.cpp entry.cpp)
    target_link_libraries(slide-tests PRIVATE Slide_impl)
    target_compile_definitions(slide-tests PRIVATE SLIDE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME slide-tests COMMAND slide-tests)
endif()
