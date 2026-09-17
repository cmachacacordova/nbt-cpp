# Agent instructions

## Project

- `nbt-cpp` is a C++23 header-only Java Edition NBT library.
- The supported API is the single class `nbt::Nbt` in `include/nbt/nbt.h`.
- The core codec has no required third-party dependencies.
- CMake exports the interface target `nbt::nbt`; keep `find_package(nbt-cpp CONFIG REQUIRED)` working.
- Keep text files LF-only and use LLVM formatting conventions.
- Use lowerCamelCase for functions, methods, fields, parameters and local variables; use PascalCase for types and enum members.

## Required behavior

- Opening binary NBT validates and indexes its complete structure without materializing values.
- Values and owning trees are materialized only when requested.
- Owned input uses `std::unique_ptr<std::byte[]>`; borrowed input does not extend source lifetime.
- Views and the private structural index retain offsets only and must not retain pointers into individual values.
- Borrowed input can be replaced with a larger span after `NeedMoreData`; `feed` accumulates fragmented input.
- File NBT uses a named root. Network NBT requires an unnamed root compound.
- Truncated input returns `Nbt::Status::NeedMoreData`; malformed input throws `Nbt::Error`.
- Native CPU instructions must remain opt-in.

## Build and verify

```sh
cmake -S . -B build-vcpkg -DCMAKE_TOOLCHAIN_FILE=C:/Workspace/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build-vcpkg --config Debug
ctest --test-dir build-vcpkg -C Debug --output-on-failure
cmake --build build-vcpkg --config Release
ctest --test-dir build-vcpkg -C Release --output-on-failure
```

Run `git diff --check` before completing changes.
