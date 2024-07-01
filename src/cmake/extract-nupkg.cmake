cmake_minimum_required(VERSION 3.20)

set(URL "https://www.nuget.org/api/v2/package/${name}/${version}")
set(NUPKG "${local_dir}/${name}-${version}.nupkg")
set(DIR "${local_dir}/${name}-${version}")

if(NOT EXISTS "${NUPKG}")
  file(DOWNLOAD "${URL}" "${NUPKG}")
endif()

if(NOT EXISTS "${DIR}")
  execute_process(
    COMMAND ${CMAKE_COMMAND} -E make_directory ${DIR}
  )
  execute_process(
    COMMAND ${CMAKE_COMMAND} -E tar xzf ${NUPKG}
    WORKING_DIRECTORY ${DIR}
  )
endif()
