find_package(Git)
# Enable DownloadProject CMake module
include(DownloadProject)

if(WIN32)
set(patch_file "0001-MSVC-build-pass.patch")
else()
set(patch_file "partio.patch")
endif()

download_project(
  PROJ partio
  GIT_REPOSITORY https://github.com/wdas/partio.git
  GIT_TAG v1.1.0
  UPDATE_DISCONNECTED 1
  PATCH_COMMAND git apply "${CMAKE_CURRENT_SOURCE_DIR}/${patch_file}"
  )
message(${GIT_EXECUTABLE})
add_subdirectory(${partio_SOURCE_DIR} ${partio_BINARY_DIR})
set_target_properties(partio PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${partio_SOURCE_DIR}/src/lib"
    INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${partio_SOURCE_DIR}/src/lib"
    #INTERFACE_LINK_LIBRARIES "z"
)