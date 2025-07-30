set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

cmake_minimum_required(VERSION 3.18)
message(STATUS "Cmake version ${CMAKE_VERSION}")

project(storage_server
  VERSION 1.1.0
  LANGUAGES CXX C)

if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()

cmake_policy(SET CMP0079 NEW)

if(POLICY CMP0167)
  cmake_policy(SET CMP0167 NEW)
endif()

option(DISABLE_SNODE_SIGNATURE "Generate and verify signatures for inter-snode communication" OFF)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
#set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
#set(CMAKE_CXX_EXTENSIONS OFF)

add_definitions(-DBOOST_NO_CXX98_FUNCTION_BASE)

cmake_policy(SET CMP0069 NEW)
option(USE_LTO "Use Link-Time Optimization" OFF)
if(USE_LTO AND NOT DEFINED CMAKE_INTERPROCEDURAL_OPTIMIZATION)
  include(CheckIPOSupported)
  check_ipo_supported(RESULT IPO_ENABLED OUTPUT ipo_error)
  if(IPO_ENABLED)
    message(STATUS "LTO enabled")
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
  else()
    message(WARNING "LTO not supported by compiler: ${ipo_error}")
  endif()
else()
  message(STATUS "LTO disabled")
  set(IPO_ENABLED OFF)
endif()

if (DISABLE_SNODE_SIGNATURE)
    add_definitions(-DDISABLE_SNODE_SIGNATURE)
endif()

list (APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")

find_package(Git)
option(MANUAL_SUBMODULES "Don't check for submodules" OFF)
if(NOT GIT_FOUND)
  message(WARNING "Git not found, unable to clone submodules")
else()
  message(STATUS "Cloning Submodules")
  execute_process(COMMAND git "submodule" "update" "--init" "--recursive" WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endif()

option(BUILD_STATIC_DEPS "Download, build and statically link against core dependencies" ON)
if(BUILD_STATIC_DEPS)
  include(StaticBuild)
else()
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(SODIUM REQUIRED IMPORTED_TARGET libsodium>=1.0.18)
  add_library(sodium INTERFACE)
  target_link_libraries(sodium INTERFACE PkgConfig::SODIUM)

  find_package(Boost 1.68.0 QUIET REQUIRED COMPONENTS system program_options filesystem)
  find_package(OpenSSL REQUIRED)
endif()

#include(cmake/check_for_std_filesystem.cmake)

add_subdirectory(common)
add_subdirectory(utils)
add_subdirectory(crypto)
add_subdirectory(storage)
add_subdirectory(httpserver)
