vcpkg_from_git(
    OUT_SOURCE_PATH SOURCE_PATH
    URL git@github.com:cmachacacordova/nbt-cpp.git
    REF e3aa068e48614998216cf7d8bac44e2dc39d522c
)

vcpkg_replace_string(
    "${SOURCE_PATH}/CMakeLists.txt"
    "add_library(nbt::nbt ALIAS nbt-cpp)"
    "add_library(nbt::nbt ALIAS nbt-cpp)\nset_target_properties(nbt-cpp PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON POSITION_INDEPENDENT_CODE ON)"
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DNBT_CPP_BUILD_TESTS=OFF
        -DNBT_CPP_BUILD_EXAMPLES=OFF
        -DNBT_CPP_ENABLE_IPO=OFF
)
vcpkg_cmake_install()

file(WRITE "${CURRENT_PACKAGES_DIR}/lib/cmake/nbt-cpp/nbt-cpp-config.cmake"
"include(CMakeFindDependencyMacro)\nfind_dependency(ZLIB)\ninclude(\"\${CMAKE_CURRENT_LIST_DIR}/nbt-cpp-targets.cmake\")\n")
file(GLOB targetFiles
    "${CURRENT_PACKAGES_DIR}/lib/cmake/nbt-cpp/*.cmake"
    "${CURRENT_PACKAGES_DIR}/debug/lib/cmake/nbt-cpp/*.cmake"
)
foreach(targetFile IN LISTS targetFiles)
    vcpkg_replace_string("${targetFile}" "nbt::nbt-cpp" "nbt::nbt" IGNORE_UNCHANGED)
endforeach()
vcpkg_cmake_config_fixup(PACKAGE_NAME nbt-cpp CONFIG_PATH lib/cmake/nbt-cpp)

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
vcpkg_copy_pdbs()
vcpkg_install_copyright(FILE_LIST "${CMAKE_CURRENT_LIST_DIR}/LICENSE")
file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/usage" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
