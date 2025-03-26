add_library(rstd.sys)
add_library(rstd::sys ALIAS rstd.sys)
target_compile_features(rstd.sys PUBLIC cxx_std_20)
target_link_libraries(rstd.sys PUBLIC rstd.core)
target_sources(
  rstd.sys
  PRIVATE pal/unix/futex.cpp pal/windows/futex.cpp sync/once/futex.cpp
          sync/thread_parking/futex.cpp sync/thread_parking/pthread.cpp)
target_sources(
  rstd.sys
  PUBLIC FILE_SET
         modules
         TYPE
         CXX_MODULES
         FILES
         mod.cppm
         pal/mod.cppm
         pal/unix/futex.cppm
         pal/windows/futex.cppm
         sync/once/mod.cppm
         sync/once/futex.cppm
         sync/thread_parking/mod.cppm
         sync/thread_parking/futex.cppm
         sync/thread_parking/pthread.cppm)
