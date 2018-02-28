
cmake_minimum_required (VERSION 2.8)
project(GE-TOOLS)

#Set the build type to Release if not specified
if (NOT CMAKE_BUILD_TYPE)
  set (CMAKE_BUILD_TYPE Release CACHE STRING
    "Choose the type of build, options are: None Debug Release RelWithDebInfo MinSizeRel."
    FORCE)
endif (NOT CMAKE_BUILD_TYPE)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

set(OX_INSTALL_DIRECTORY "/usr/local/orchestra" CACHE PATH "Install directory of the GE Orchestra SDK")
set(ENV{ISMRMRD_HOME} "~/ismrmrd" CACHE PATH "Install directory of ISMRMRD")
set(LARGE_LIBRARY_TYPE STATIC)

set(ENV{HDF5_ROOT} ${OX_INSTALL_DIRECTORY}/include/recon/3p/Linux/hdf5-1.8.12_dev_linux64)

find_package(LibXslt REQUIRED)
find_package(LibXml2 REQUIRED)
find_package(Ismrmrd REQUIRED)
find_package(HDF5 REQUIRED)
find_package(Orchestra)

add_definitions(-std=c++11)
#SET(BUILD_SHARED_LIBRARIES OFF)
#SET(CMAKE_EXE_LINKER_FLAGS "-static")

# From http://xit0.org/2013/04/cmake-use-git-branch-and-commit-details-in-project/
# Get the current working branch
execute_process(
  COMMAND git rev-parse --abbrev-ref HEAD
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE GIT_BRANCH
  OUTPUT_STRIP_TRAILING_WHITESPACE
  )
# Get the latest abbreviated commit hash of the working branch
execute_process(
  COMMAND git log -1 --format=%h
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE GIT_COMMIT_HASH
  OUTPUT_STRIP_TRAILING_WHITESPACE
  )
add_definitions("-DGIT_BRANCH=\"${GIT_BRANCH}\"")
add_definitions("-DGIT_COMMIT_HASH=\"${GIT_COMMIT_HASH}\"")

# build C++ converter
add_subdirectory(src)

add_custom_command(
  OUTPUT tags
  COMMAND ctags -R --languages=C,+C++ ${CMAKE_SOURCE_DIR}
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
add_custom_target(ctags DEPENDS tags)
