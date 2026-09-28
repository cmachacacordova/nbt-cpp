function(nbt_detect_host_endianness output_variable)
  set(_detector_source "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/detect_endianness.cpp")

  try_run(
    _run_result
    _compile_result
    "${CMAKE_CURRENT_BINARY_DIR}/cmake/endianness"
    "${_detector_source}"
    CMAKE_FLAGS "-DCMAKE_CXX_STANDARD=23"
    RUN_OUTPUT_VARIABLE _run_output)

  if(NOT _compile_result)
    message(FATAL_ERROR "Failed to compile the host-endianness detector.\n${_run_output}")
  endif()

  if(_run_result EQUAL 0)
    set(${output_variable} 1 PARENT_SCOPE)
    message(STATUS "Host endianness: big-endian (matches Java Edition / network NBT)")
  else()
    set(${output_variable} 0 PARENT_SCOPE)
    message(STATUS "Host endianness: little-endian (byte swap required for NBT)")
  endif()
endfunction()
