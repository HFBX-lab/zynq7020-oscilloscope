cmake_minimum_required(VERSION 3.20)
project(AdcInternalSelftest C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
foreach(path BSP_INCLUDE BSP_LIB)
  file(TO_CMAKE_PATH "${${path}}" ${path})
endforeach()
add_executable(adc_internal_selftest app.c adc_format.c board.c portable.c frame_source.c memory_probe.c)
target_include_directories(adc_internal_selftest PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${BSP_INCLUDE}")
target_compile_options(adc_internal_selftest PRIVATE -pipe -O2 -g -mcpu=cortex-a9 -mfpu=vfpv3
  -mfloat-abi=hard -ffunction-sections -fdata-sections -Wall -Wextra -Werror)
target_link_directories(adc_internal_selftest PRIVATE "${BSP_LIB}")
target_link_options(adc_internal_selftest PRIVATE -mcpu=cortex-a9 -mfpu=vfpv3 -mfloat-abi=hard
  "-specs=${CMAKE_CURRENT_SOURCE_DIR}/Xilinx.spec" "-Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/lscript.ld"
  "-Wl,-Map,${CMAKE_BINARY_DIR}/adc_internal_selftest.map" -Wl,--gc-sections)
target_link_libraries(adc_internal_selftest PRIVATE -Wl,--start-group xil gcc c m -Wl,--end-group)
set_target_properties(adc_internal_selftest PROPERTIES SUFFIX ".elf")
