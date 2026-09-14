cmake_minimum_required(VERSION 3.20)
project(nbt-cpp VERSION 0.1.0 LANGUAGES CXX)

option(NBT_CPP_BUILD_TESTS "Build nbt-cpp tests" ON)
option(NBT_CPP_BUILD_EXAMPLES "Build nbt-cpp examples" ON)
option(NBT_CPP_ENABLE_IPO "Enable interprocedural optimization in Release builds" ON)
option(NBT_CPP_NATIVE_ARCH "Optimize for the build machine CPU" OFF)

find_package(ZLIB REQUIRED)

add_library(nbt-cpp src/nbt.cpp)
add_library(nbt::nbt ALIAS nbt-cpp)
set_target_properties(nbt-cpp PROPERTIES
    EXPORT_NAME nbt
    POSITION_INDEPENDENT_CODE ON)
if(BUILD_SHARED_LIBS)
    target_compile_definitions(nbt-cpp PRIVATE NBT_CPP_EXPORTS PUBLIC NBT_CPP_SHARED)
endif()
target_compile_features(nbt-cpp PUBLIC cxx_std_20)
target_include_directories(nbt-cpp PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_link_libraries(nbt-cpp PRIVATE ZLIB::ZLIB)

if(MSVC)
    target_compile_options(nbt-cpp PRIVATE /W4 /permissive-)
    if(NBT_CPP_NATIVE_ARCH)
        target_compile_options(nbt-cpp PRIVATE /arch:AVX2)
    endif()
else()
    target_compile_options(nbt-cpp PRIVATE -Wall -Wextra -Wpedantic)
    if(NBT_CPP_NATIVE_ARCH)
        target_compile_options(nbt-cpp PRIVATE -march=native -mtune=native)
    endif()
endif()

if(NBT_CPP_ENABLE_IPO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT NBT_CPP_IPO_SUPPORTED)
    if(NBT_CPP_IPO_SUPPORTED)
        set_property(TARGET nbt-cpp PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
    endif()
endif()

if(NBT_CPP_BUILD_EXAMPLES)
    add_executable(nbt-read-gzip examples/readGzip.cpp)
    add_executable(nbt-query-views examples/queryViews.cpp)
    add_executable(nbt-build examples/buildNbt.cpp)
    add_executable(nbt-snbt examples/snbt.cpp)
    add_executable(nbt-buffer-chain examples/bufferChain.cpp)
    add_executable(nbt-network examples/networkNbt.cpp)
    target_link_libraries(nbt-read-gzip PRIVATE nbt::nbt)
    target_link_libraries(nbt-query-views PRIVATE nbt::nbt)
    target_link_libraries(nbt-build PRIVATE nbt::nbt)
    target_link_libraries(nbt-snbt PRIVATE nbt::nbt)
    target_link_libraries(nbt-buffer-chain PRIVATE nbt::nbt)
    target_link_libraries(nbt-network PRIVATE nbt::nbt)
    target_compile_definitions(nbt-read-gzip PRIVATE
        NBT_CPP_EXAMPLE_LEVEL_PATH="${CMAKE_CURRENT_SOURCE_DIR}/tests/level.dat")
    if(NBT_CPP_ENABLE_IPO AND NBT_CPP_IPO_SUPPORTED)
        foreach(exampleTarget IN ITEMS nbt-read-gzip nbt-query-views nbt-build nbt-snbt nbt-buffer-chain nbt-network)
            set_property(TARGET ${exampleTarget} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
        endforeach()
    endif()
endif()

if(NBT_CPP_BUILD_TESTS)
    enable_testing()
    add_executable(nbt-cpp-tests tests/nbt_tests.cpp)
    target_link_libraries(nbt-cpp-tests PRIVATE nbt::nbt)
    target_compile_definitions(nbt-cpp-tests PRIVATE
        NBT_CPP_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests")
    if(NBT_CPP_ENABLE_IPO AND NBT_CPP_IPO_SUPPORTED)
        set_property(TARGET nbt-cpp-tests PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
    endif()
    add_test(NAME nbt-cpp-tests COMMAND nbt-cpp-tests)
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(nbtCppCmakeDir "${CMAKE_INSTALL_LIBDIR}/cmake/nbt-cpp")
configure_package_config_file(
    cmake/nbt-cpp-config.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/nbt-cpp-config.cmake"
    INSTALL_DESTINATION "${nbtCppCmakeDir}")
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/nbt-cpp-config-version.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)

install(TARGETS nbt-cpp EXPORT nbt-cpp-targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
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
