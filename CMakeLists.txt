cmake_minimum_required(VERSION 3.16)
project(fftviz CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# Taşınabilir çekirdek: core/ içinde libc/heap/float kullanımı yasak.
file(GLOB FVCORE_SRC CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/core/src/*.cpp)
add_library(fvcore STATIC ${FVCORE_SRC})
target_include_directories(fvcore PUBLIC ${CMAKE_SOURCE_DIR}/core/src)
target_compile_options(fvcore PRIVATE -Wall -Wextra -Wno-unused-parameter -fno-exceptions -fno-rtti)

# Tablo üreticileri (host araçları; double kullanabilir).
add_executable(gen_tables tools/gen_tables.cpp)
target_include_directories(gen_tables PRIVATE ${CMAKE_SOURCE_DIR}/core/src)
add_executable(gen_vis_tables tools/gen_vis_tables.cpp)
target_include_directories(gen_vis_tables PRIVATE ${CMAKE_SOURCE_DIR}/core/src)

# Golden hash üreticisi: tests/golden/golden.json ve core/src/fv_golden_gen.h.
add_executable(gen_golden tools/gen_golden.cpp)
target_link_libraries(gen_golden PRIVATE fvcore)

enable_testing()
file(GLOB FV_TESTS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/tests/test_*.cpp)
foreach(src ${FV_TESTS})
  get_filename_component(name ${src} NAME_WE)
  add_executable(${name} ${src})
  target_link_libraries(${name} PRIVATE fvcore)
  target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR}/tests)
  target_compile_definitions(${name} PRIVATE FV_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
                             FV_BINARY_DIR="${CMAKE_BINARY_DIR}")
  if(name MATCHES "^(test_v2_pins|test_v2_controls|test_ir_control|test_ir_cli|test_ir_integration|test_ir_disabled|test_task_lifecycle|test_selftest_input|test_standby_diagnostics)$")
    target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR}/tests/fixtures/v2)
  endif()
  add_test(NAME ${name} COMMAND ${name} WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endforeach()

# Drift testleri üreticileri çalıştırır; önce derlenmiş olmaları gerekir.
if(TARGET test_tables_drift)
  add_dependencies(test_tables_drift gen_tables)
endif()
if(TARGET test_vis_tables_drift)
  add_dependencies(test_vis_tables_drift gen_vis_tables)
endif()
if(TARGET test_ir_integration)
  add_dependencies(test_ir_integration test_ir_disabled)
endif()
