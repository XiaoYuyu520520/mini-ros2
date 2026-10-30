# Collects the MinGW runtime DLLs an executable needs and copies them next to
# it, so a build directory is runnable on a machine that has no MSYS2 at all.
#
#   cmake -DEXE=path\to\app.exe -DDEST=path\to\out -DSEARCH=path\to\ucrt64\bin
#         -P bundle_runtime.cmake
#
# Only the non-system dependencies are copied: everything the executable pulls
# out of the MSYS2 prefix (libstdc++-6, libwinpthread-1, the gRPC/protobuf/
# abseil DLLs) lands in DEST, and KERNEL32/api-ms-win-crt-* stay where Windows
# keeps them. The Windows loader searches an executable's own directory first,
# so the bundled copy wins over a stale one on PATH.

if(POLICY CMP0207)
  cmake_policy(SET CMP0207 NEW)
endif()

if(NOT EXISTS "${EXE}")
  message(WARNING "bundle_runtime: ${EXE} does not exist, nothing bundled")
  return()
endif()

set(_dirs)
if(EXISTS "${SEARCH}")
  list(APPEND _dirs "${SEARCH}")
endif()
foreach(_extra IN LISTS EXTRA_DIRS)
  if(EXISTS "${_extra}")
    list(APPEND _dirs "${_extra}")
  endif()
endforeach()

file(GET_RUNTIME_DEPENDENCIES
  EXECUTABLES "${EXE}"
  RESOLVED_DEPENDENCIES_VAR _resolved
  UNRESOLVED_DEPENDENCIES_VAR _unresolved
  DIRECTORIES ${_dirs}
  PRE_EXCLUDE_REGEXES "^api-ms-" "^ext-ms-" "^KERNEL32" "^USER32" "^ADVAPI32"
  POST_EXCLUDE_REGEXES ".*[Ss]ystem32.*" ".*[Ww]indows/[Ss]ystem32.*")

set(_copied 0)
foreach(_dep IN LISTS _resolved)
  get_filename_component(_name "${_dep}" NAME)
  if(EXISTS "${DEST}/${_name}")
    continue()
  endif()
  file(COPY "${_dep}" DESTINATION "${DEST}")
  math(EXPR _copied "${_copied} + 1")
endforeach()

if(_unresolved)
  message(WARNING "bundle_runtime: unresolved dependencies: ${_unresolved}")
endif()
message(STATUS "bundle_runtime: ${_copied} DLL(s) bundled next to ${EXE}")
