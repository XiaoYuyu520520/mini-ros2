# FindgRPC.cmake - locate a gRPC installation that ships no CMake package.
#
# Why this exists: both trees that need gRPC (the bridge and the gRPC SDK)
# ask for `find_package(gRPC CONFIG REQUIRED)`. That is the right way to do it
# on Windows, where MSYS2 ships gRPCConfig.cmake, and it works on Ubuntu
# x86_64, where libgrpc++-dev installs the same file next to the libraries.
# Ubuntu 22.04's arm64 archive does not ship it, so the configure step stopped
# with "Could not find a package configuration file provided by gRPC".
#
# This module is the fallback for exactly that case: it finds the same pieces
# the package config would have provided and defines the same imported
# targets, so nothing downstream has to know which route was taken.
#
#   gRPC::grpc++            the C++ runtime library
#   gRPC::grpc_cpp_plugin   the protoc plugin that generates the gRPC stubs
#   gRPC::grpc++_reflection the reflection library, when it is present
#
# It is a module, not a config package, so `find_package(gRPC MODULE)` finds it
# through CMAKE_MODULE_PATH. The callers try CONFIG first and fall back here.
#
# This copy lives in the SDK tree because the SDK is built on its own too (the
# agent is built on the robot, where the SDK's own CMakeLists.txt does the
# lookup). The two copies are the same file; keep them in step.

include(FindPackageHandleStandardArgs)

find_path(gRPC_INCLUDE_DIR
  NAMES grpcpp/grpcpp.h
  PATH_SUFFIXES include)

find_library(gRPC_LIBRARY
  NAMES grpc++
  PATH_SUFFIXES lib lib64)

find_library(gRPC_REFLECTION_LIBRARY
  NAMES grpc++_reflection
  PATH_SUFFIXES lib lib64)

find_library(gRPC_CPP_PLUGIN
  NAMES grpc_cpp_plugin
  PATH_SUFFIXES lib lib64)

find_package_handle_standard_args(gRPC
  REQUIRED_VARS gRPC_INCLUDE_DIR gRPC_LIBRARY gRPC_CPP_PLUGIN)

if(gRPC_FOUND AND NOT TARGET gRPC::grpc++)
  add_library(gRPC::grpc++ UNKNOWN IMPORTED)
  set_target_properties(gRPC::grpc++ PROPERTIES
    IMPORTED_LOCATION "${gRPC_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${gRPC_INCLUDE_DIR}")

  if(gRPC_REFLECTION_LIBRARY)
    add_library(gRPC::grpc++_reflection UNKNOWN IMPORTED)
    set_target_properties(gRPC::grpc++_reflection PROPERTIES
      IMPORTED_LOCATION "${gRPC_REFLECTION_LIBRARY}"
      INTERFACE_INCLUDE_DIRECTORIES "${gRPC_INCLUDE_DIR}")
  endif()

  add_executable(gRPC::grpc_cpp_plugin IMPORTED)
  set_target_properties(gRPC::grpc_cpp_plugin PROPERTIES
    IMPORTED_LOCATION "${gRPC_CPP_PLUGIN}")

  find_package(Threads REQUIRED)
  set_property(TARGET gRPC::grpc++ APPEND PROPERTY
    INTERFACE_LINK_LIBRARIES Threads::Threads)
endif()

mark_as_advanced(gRPC_INCLUDE_DIR gRPC_LIBRARY gRPC_REFLECTION_LIBRARY gRPC_CPP_PLUGIN)