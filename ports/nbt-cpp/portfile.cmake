vcpkg_from_git(
    OUT_SOURCE_PATH SOURCE_PATH
    URL git@github.com:cmachacacordova/nbt-cpp.git
    REF a64a795562520500682d16af9dd3b7684a674b35
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DNBT_CPP_BUILD_TESTS=OFF
        -DNBT_CPP_BUILD_EXAMPLES=OFF
)
vcpkg_cmake_install()
file(GLOB targetFiles
    "${CURRENT_PACKAGES_DIR}/lib/cmake/nbt-cpp/*.cmake"
    "${CURRENT_PACKAGES_DIR}/debug/lib/cmake/nbt-cpp/*.cmake"
)
foreach(targetFile IN LISTS targetFiles)
    vcpkg_replace_string("${targetFile}" "nbt::nbt-cpp" "nbt::nbt" IGNORE_UNCHANGED)
endforeach()
vcpkg_cmake_config_fixup(PACKAGE_NAME nbt-cpp CONFIG_PATH lib/cmake/nbt-cpp)

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/share")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
vcpkg_copy_pdbs()
vcpkg_install_copyright(FILE_LIST "${CMAKE_CURRENT_LIST_DIR}/LICENSE")
file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/usage" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
