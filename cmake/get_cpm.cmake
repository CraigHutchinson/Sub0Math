# cmake/get_cpm.cmake — download CPM.cmake on first configure if not cached.
# Pin the version here; bump deliberately when updating.
set(CPM_DOWNLOAD_VERSION 0.43.2)
# Prefer the shared cache (CPM_SOURCE_CACHE, docs/DEV_SETUP.md) so the bootstrap is downloaded once per machine, not per build dir.
if(DEFINED ENV{CPM_SOURCE_CACHE} AND NOT "$ENV{CPM_SOURCE_CACHE}" STREQUAL "")
  set(CPM_DOWNLOAD_LOCATION "$ENV{CPM_SOURCE_CACHE}/cpm/CPM_${CPM_DOWNLOAD_VERSION}.cmake")
  file(MAKE_DIRECTORY "$ENV{CPM_SOURCE_CACHE}/cpm")
else()
  set(CPM_DOWNLOAD_LOCATION "${CMAKE_BINARY_DIR}/cmake/CPM_${CPM_DOWNLOAD_VERSION}.cmake")
endif()
set(CPM_DOWNLOAD_SHA256 "49A3BEF91CEB65BB66D57255E12D1FFD22ABC2F6408FA9FE4534C544A2F232AA")
set(CPM_DOWNLOAD_TEMPORARY "${CPM_DOWNLOAD_LOCATION}.tmp")

if(NOT EXISTS "${CPM_DOWNLOAD_LOCATION}")
  file(REMOVE "${CPM_DOWNLOAD_TEMPORARY}")
  message(STATUS "Downloading CPM.cmake v${CPM_DOWNLOAD_VERSION}...")
  file(DOWNLOAD
    "https://github.com/cpm-cmake/CPM.cmake/releases/download/v${CPM_DOWNLOAD_VERSION}/CPM.cmake"
    "${CPM_DOWNLOAD_TEMPORARY}"
    TIMEOUT 60
    EXPECTED_HASH "SHA256=${CPM_DOWNLOAD_SHA256}"
    STATUS CPM_DOWNLOAD_STATUS
    LOG CPM_DOWNLOAD_LOG
  )
  list(GET CPM_DOWNLOAD_STATUS 0 CPM_DOWNLOAD_CODE)
  if(NOT CPM_DOWNLOAD_CODE EQUAL 0)
    file(REMOVE "${CPM_DOWNLOAD_TEMPORARY}")
    message(FATAL_ERROR
      "Unable to download CPM.cmake v${CPM_DOWNLOAD_VERSION}: "
      "${CPM_DOWNLOAD_STATUS}\n${CPM_DOWNLOAD_LOG}")
  endif()
  file(RENAME "${CPM_DOWNLOAD_TEMPORARY}" "${CPM_DOWNLOAD_LOCATION}" RESULT CPM_RENAME_RESULT)
  if(NOT CPM_RENAME_RESULT STREQUAL "0")
    file(REMOVE "${CPM_DOWNLOAD_TEMPORARY}")
    message(FATAL_ERROR "Unable to cache CPM.cmake: ${CPM_RENAME_RESULT}")
  endif()
else()
  file(SHA256 "${CPM_DOWNLOAD_LOCATION}" CPM_CACHED_SHA256)
  string(TOUPPER "${CPM_CACHED_SHA256}" CPM_CACHED_SHA256)
  if(NOT CPM_CACHED_SHA256 STREQUAL CPM_DOWNLOAD_SHA256)
    message(WARNING
      "Cached CPM.cmake v${CPM_DOWNLOAD_VERSION} has an unexpected SHA-256; removing it and retrying.")
    file(REMOVE "${CPM_DOWNLOAD_LOCATION}")
    include("${CMAKE_CURRENT_LIST_FILE}")
    return()
  endif()
endif()

# CPM.cmake records the directory it was loaded from in the cache (CPM_DIRECTORY). On a later configure, finding that entry set to another
# directory, it assumes a parent project already loaded CPM and returns without defining CPMAddPackage ("Unknown CMake command"). The entry goes
# stale whenever CPM_SOURCE_CACHE changes between configures of one build directory (a user-level cache set after the build was made). Forget it
# unless CPM really is loaded in this run already (a parent project that includes this one).
if(NOT COMMAND CPMAddPackage AND DEFINED CACHE{CPM_DIRECTORY})
  get_filename_component(_getCpmDir "${CPM_DOWNLOAD_LOCATION}" DIRECTORY)
  get_filename_component(_getCpmDir "${_getCpmDir}" REALPATH)
  if(NOT CPM_DIRECTORY STREQUAL _getCpmDir)
    message(STATUS "CPM.cmake: this build last loaded it from ${CPM_DIRECTORY}; now from ${_getCpmDir} (CPM_SOURCE_CACHE changed)")
    unset(CPM_DIRECTORY CACHE)
  endif()
  unset(_getCpmDir)
endif()

include("${CPM_DOWNLOAD_LOCATION}")
