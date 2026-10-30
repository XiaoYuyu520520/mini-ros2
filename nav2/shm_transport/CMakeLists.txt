cmake_minimum_required(VERSION 3.16)
project(nav2_shm_transport CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# The POSIX backend puts the writer mutex and the reader condition variable in
# the shared segment itself (see src/shm_transport.cpp), so the library needs
# the threading flags on every platform, not just in the test.
find_package(Threads REQUIRED)

add_library(shm_transport
  src/shm_transport.cpp
  src/shm_transport_c.cpp
)
target_include_directories(shm_transport PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(shm_transport PUBLIC Threads::Threads)

# Shared library exposing the C ABI for Python (ctypes) consumers.
add_library(shm_transport_shared SHARED
  src/shm_transport.cpp
  src/shm_transport_c.cpp
)
target_include_directories(shm_transport_shared PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(shm_transport_shared PUBLIC Threads::Threads)
set_target_properties(shm_transport_shared PROPERTIES OUTPUT_NAME "shm_transport")
if(MINGW)
  target_link_options(shm_transport_shared PRIVATE
    -Wl,--export-all-symbols -static -static-libgcc -static-libstdc++)
elseif(MSVC)
  set_target_properties(shm_transport_shared PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

if(MSVC)
  target_compile_options(shm_transport PRIVATE /W4)
  target_compile_options(shm_transport_shared PRIVATE /W4)
else()
  target_compile_options(shm_transport PRIVATE -Wall -Wextra)
  target_compile_options(shm_transport_shared PRIVATE -Wall -Wextra)
endif()

# glibc moved shm_open() into libc in 2.34; older ones keep it in librt. Ask
# for rt only when it is actually there, so a modern system links nothing.
if(UNIX)
  include(CheckLibraryExists)
  check_library_exists(rt shm_open "" SHM_TRANSPORT_HAVE_LIBRT)
  if(SHM_TRANSPORT_HAVE_LIBRT)
    target_link_libraries(shm_transport PUBLIC rt)
    target_link_libraries(shm_transport_shared PUBLIC rt)
  endif()
endif()

add_executable(test_shm tests/test_shm.cpp)
target_link_libraries(test_shm PRIVATE shm_transport)

enable_testing()
add_test(NAME shm_transport COMMAND test_shm)

# Same reason as the helper in tests/test_shm.cpp: the channel names carry
# NAV2_SHM_NAMESPACE so that two copies of this test - this build and the one
# under bridge/windows/build - cannot take each other's publish mutex.
set_tests_properties(shm_transport PROPERTIES
  ENVIRONMENT_MODIFICATION "NAV2_SHM_NAMESPACE=set:ctest_shm_transport"
  # A channel whose writer was killed leaves the next run waiting for a wakeup
  # that never comes - no CPU, no output, no end (the POSIX half of this is the
  # comment above `test_abandoned_channel`; on Windows the corpse comes from the
  # daemon the bridge tree is rebuilt against, which is stopped with
  # `taskkill`). Without a bound that hang is indistinguishable from a test
  # that is merely slow, and it takes the whole tree with it. Five minutes is
  # three orders of magnitude over the one second this test needs.
  TIMEOUT 300)
