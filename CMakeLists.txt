cmake_minimum_required(VERSION 3.20)
project(nbt-cpp VERSION 0.2.0 LANGUAGES CXX)

option(NBT_CPP_BUILD_TESTS "Build nbt-cpp tests" ON)
option(NBT_CPP_BUILD_EXAMPLES "Build nbt-cpp examples" ON)
option(NBT_CPP_BUILD_UTILITIES "Expose SNBT, filesystem and compression utilities" ON)
option(NBT_STRICT_MODE "Optimize encoding for well-formed input; invalid values produce exceptions or UB instead of being ignored" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/endianness")
include(DetectHostEndianness)
nbt_detect_host_endianness(NBT_BIG_ENDIAN)

add_library(nbt-cpp INTERFACE)
add_library(nbt::nbt ALIAS nbt-cpp)
set_target_properties(nbt-cpp PROPERTIES EXPORT_NAME nbt)
target_compile_features(nbt-cpp INTERFACE cxx_std_23)
target_include_directories(nbt-cpp INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)

if (NBT_BIG_ENDIAN)
target_compile_definitions(nbt-cpp INTERFACE NBT_BIG_ENDIAN=${NBT_BIG_ENDIAN})
endif()

if (NBT_STRICT_MODE)
target_compile_definitions(nbt-cpp INTERFACE NBT_STRICT_MODE)
endif()

if(NBT_CPP_BUILD_UTILITIES)
    add_library(nbt-cpp-utilities INTERFACE)
    add_library(nbt::utilities ALIAS nbt-cpp-utilities)
    set_target_properties(nbt-cpp-utilities PROPERTIES EXPORT_NAME utilities)

    find_package(ZLIB REQUIRED)
    target_link_libraries(nbt-cpp-utilities INTERFACE nbt-cpp ZLIB::ZLIB)

    find_path(ZSTR_INCLUDE_DIRS "zstr.hpp")
    target_include_directories(nbt-cpp-utilities INTERFACE $<BUILD_INTERFACE:${ZSTR_INCLUDE_DIRS}>)
endif()

if(NBT_CPP_BUILD_TESTS)
    enable_testing()
    find_package(GTest CONFIG REQUIRED)
    include(GoogleTest)

    add_executable(nbt-cpp-tests tests/nbt_tests.cpp)
    target_link_libraries(nbt-cpp-tests PRIVATE nbt::nbt GTest::gtest_main)
    gtest_discover_tests(nbt-cpp-tests
        TEST_PREFIX "core."
        PROPERTIES LABELS "core;codec")

    if(NBT_CPP_BUILD_UTILITIES)
        add_executable(nbt-cpp-utilities-tests tests/utilities_tests.cpp)
        target_link_libraries(nbt-cpp-utilities-tests PRIVATE nbt::utilities GTest::gtest_main)
        gtest_discover_tests(nbt-cpp-utilities-tests
            TEST_PREFIX "utilities."
            PROPERTIES LABELS "utilities;io")
    endif()

    if(MSVC)
        target_compile_options(nbt-cpp-tests PRIVATE /W4 /permissive-)
        if(NBT_CPP_BUILD_UTILITIES)
            target_compile_options(nbt-cpp-utilities-tests PRIVATE /W4 /permissive-)
        endif()
    else()
        target_compile_options(nbt-cpp-tests PRIVATE -Wall -Wextra -Wpedantic)
        if(NBT_CPP_BUILD_UTILITIES)
            target_compile_options(nbt-cpp-utilities-tests PRIVATE -Wall -Wextra -Wpedantic)
        endif()
    endif()
endif()

if(NBT_CPP_BUILD_EXAMPLES)
    foreach(example IN ITEMS bufferChain buildNbt networkNbt)
        add_executable(nbt-example-${example} examples/${example}.cpp)
        target_link_libraries(nbt-example-${example} PRIVATE nbt::nbt)
    endforeach()
    if(NBT_CPP_BUILD_UTILITIES)
        foreach(example IN ITEMS readGzip snbt)
            add_executable(nbt-example-${example} examples/${example}.cpp)
            target_link_libraries(nbt-example-${example} PRIVATE nbt::utilities)
        endforeach()
    endif()
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
set(nbtCppCmakeDir "${CMAKE_INSTALL_LIBDIR}/cmake/nbt-cpp")
set(NBT_CPP_CONFIG_FIND_ZLIB "")
if(NBT_CPP_BUILD_UTILITIES)
    set(NBT_CPP_CONFIG_FIND_ZLIB "include(CMakeFindDependencyMacro)\nfind_dependency(ZLIB)")
endif()
configure_package_config_file(cmake/nbt-cpp-config.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/nbt-cpp-config.cmake"
    INSTALL_DESTINATION "${nbtCppCmakeDir}")
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/nbt-cpp-config-version.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)
install(TARGETS nbt-cpp EXPORT nbt-cpp-targets)
if(NBT_CPP_BUILD_UTILITIES)
    install(TARGETS nbt-cpp-utilities EXPORT nbt-cpp-targets)
endif()
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/licenses/nbt-cpp)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/nbt-cpp-config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/nbt-cpp-config-version.cmake"
    DESTINATION "${nbtCppCmakeDir}")
install(EXPORT nbt-cpp-targets
    FILE nbt-cpp-targets.cmake
    NAMESPACE nbt::
    DESTINATION "${nbtCppCmakeDir}")
