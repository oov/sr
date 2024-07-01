option(FORMAT_SOURCES "execute clang-format" ON)

if(FORMAT_SOURCES)
  file(GLOB_RECURSE sources LIST_DIRECTORIES false CONFIGURE_DEPENDS "*.h" "*.c")
  list(FILTER sources EXCLUDE REGEX "${CMAKE_CURRENT_SOURCE_DIR}/3rd")
  find_program(CLANG_FORMAT_EXE clang-format)
  add_custom_target(${PROJECT_NAME}-format ALL
    COMMAND ${CLANG_FORMAT_EXE} -style=file -i ${sources}
  )
endif()

add_subdirectory(3rd/ovbase)
add_subdirectory(3rd/ovutil)

set(DML_NAME "Microsoft.AI.DirectML")
set(DML_VERSION "1.13.1")
set(DML_DLL "${CMAKE_CURRENT_BINARY_DIR}/${DML_NAME}-${DML_VERSION}/bin/x64-win/DirectML.dll")
add_custom_target(extract_dml
  SOURCES "${CMAKE_BINARY_DIR}/bin/DirectML.dll"
)
add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/bin/DirectML.dll"
  COMMAND ${CMAKE_COMMAND}
  -Dlocal_dir="${CMAKE_CURRENT_BINARY_DIR}"
  -Dname="${DML_NAME}"
  -Dversion="${DML_VERSION}"
  -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/extract-nupkg.cmake"
  COMMAND ${CMAKE_COMMAND} -E copy "${DML_DLL}" "${CMAKE_BINARY_DIR}/bin/"
)

set(ORT_DML_NAME "Microsoft.ML.OnnxRuntime.DirectML")
set(ORT_DML_VERSION "1.18.0")
set(ORT_DML_DLL "${CMAKE_CURRENT_BINARY_DIR}/${ORT_DML_NAME}-${ORT_DML_VERSION}/runtimes/win-x64/native/onnxruntime.dll")
set(ORT_DML_INCLUDE "${CMAKE_CURRENT_BINARY_DIR}/${ORT_DML_NAME}-${ORT_DML_VERSION}/build/native/include")
add_custom_target(extract_ort_dml
  SOURCES "${CMAKE_BINARY_DIR}/bin/onnxruntime.dll"
)
add_dependencies(extract_ort_dml extract_dml)
add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/bin/onnxruntime.dll"
  COMMAND ${CMAKE_COMMAND}
  -Dlocal_dir="${CMAKE_CURRENT_BINARY_DIR}"
  -Dname="${ORT_DML_NAME}"
  -Dversion="${ORT_DML_VERSION}"
  -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/extract-nupkg.cmake"
  COMMAND ${CMAKE_COMMAND} -E copy "${ORT_DML_DLL}" "${CMAKE_BINARY_DIR}/bin/"
)

# set(ORT_CUDA_NAME "Microsoft.ML.OnnxRuntime.Gpu.Windows")
# set(ORT_CUDA_VERSION "1.18.0")
# set(ORT_CUDA_INCLUDE "${CMAKE_CURRENT_BINARY_DIR}/${ORT_CUDA_NAME}-${ORT_CUDA_VERSION}/buildTransitive/native/include")
# 
# add_custom_target(extract_ort_cuda
#   SOURCES "${CMAKE_BINARY_DIR}/bin/onnxruntime_providers_cuda.dll"
# )
# add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/bin/onnxruntime_providers_cuda.dll"
#   COMMAND ${CMAKE_COMMAND}
#   -Dlocal_dir="${CMAKE_CURRENT_BINARY_DIR}"
#   -Dname="${ORT_CUDA_NAME}"
#   -Dversion="${ORT_CUDA_VERSION}"
#   -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/extract-nupkg.cmake"
#   COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_BINARY_DIR}/${ORT_CUDA_NAME}-${ORT_CUDA_VERSION}/runtimes/win-x64/native/onnxruntime_providers_cuda.dll" "${CMAKE_BINARY_DIR}/bin/"
#   COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_BINARY_DIR}/${ORT_CUDA_NAME}-${ORT_CUDA_VERSION}/runtimes/win-x64/native/onnxruntime_providers_shared.dll" "${CMAKE_BINARY_DIR}/bin/"
# )

add_library(sr_intf INTERFACE)
target_compile_definitions(sr_intf INTERFACE
  _WIN32
  __STDC_NO_THREADS__
  _WIN32_WINNT=0x0605
  _WINDOWS
  SPNG_USE_MINIZ
  $<$<CONFIG:Release>:NDEBUG>
)
target_compile_options(sr_intf INTERFACE
  -march=x86-64-v2
  -mstackrealign
  -Wall
  -Wextra
  -Werror
  -Weverything
  -Wshadow
  -Werror=return-type
  -pedantic-errors
  -Wno-declaration-after-statement
  -Wno-padded
  -Wno-used-but-marked-unused
  -Wno-unsafe-buffer-usage
  -Wno-switch-default
  -ffunction-sections
  -fdata-sections
  -flto
  $<$<CONFIG:Debug>:-O0>
  $<$<CONFIG:Release>:-O2>
)
target_link_options(sr_intf INTERFACE
  -municode
  -mwindows
  -fuse-ld=lld
  -Wl,--gc-sections
  -Wl,--kill-at
  -static
  $<$<CONFIG:Release>:-s>
)

add_executable(sr
  image.c
  main.c
  onnx.c
  session.c
  sr.rc
)
set_target_properties(sr PROPERTIES OUTPUT_NAME sr RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
target_link_libraries(sr PRIVATE sr_intf dxgi)
add_dependencies(sr extract_ort_dml)
target_include_directories(sr BEFORE PRIVATE
  "${ORT_DML_INCLUDE}"
)
target_link_libraries(sr PRIVATE
  comctl32
  dwmapi
  ovbase
  ovutil
  "${CMAKE_BINARY_DIR}/bin/onnxruntime.dll"
)
