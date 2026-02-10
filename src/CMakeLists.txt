add_subdirectory(core)

set(RSTD_PRIVATE_SOURCES)
set(RSTD_SYS_SOURCES)
set(RSTD_IO_SOURCES)
set(RSTD_ALLOC_SOURCES)
set(RSTD_THREAD_SOURCES)
set(RSTD_SYNC_SOURCES)

add_subdirectory(sys)
add_subdirectory(alloc)
add_subdirectory(io)
add_subdirectory(thread)
add_subdirectory(sync)

add_library(rstd)
add_library(rstd::rstd ALIAS rstd)
target_compile_features(rstd PUBLIC cxx_std_23)
target_link_libraries(rstd PUBLIC rstd.core)

target_sources(rstd PRIVATE ${ALLOC_SOURCE_FILES})
target_sources(
  rstd
  PUBLIC FILE_SET
         modules
         TYPE
         CXX_MODULES
         FILES
         mod.cppm
         error.cppm
         ${RSTD_SYS_SOURCES}
         ${RSTD_ALLOC_SOURCES}
         ${RSTD_IO_SOURCES}
         ${RSTD_THREAD_SOURCES}
         ${RSTD_SYNC_SOURCES})

target_sources(rstd PRIVATE ${RSTD_PRIVATE_SOURCES})
