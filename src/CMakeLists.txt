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

add_library(rstd_)
target_compile_features(rstd_ PUBLIC cxx_std_23)
target_link_libraries(rstd_ PUBLIC rstd.core)
set_target_properties(rstd_ PROPERTIES OUTPUT_NAME rstd)

target_sources(
  rstd_
  PUBLIC FILE_SET
         modules
         TYPE
         CXX_MODULES
         FILES
         mod.cppm
         error.cppm
         panicking.cppm
         process.cppm
         ${RSTD_SYS_SOURCES}
         ${RSTD_ALLOC_SOURCES}
         ${RSTD_IO_SOURCES}
         ${RSTD_THREAD_SOURCES}
         ${RSTD_SYNC_SOURCES})

target_sources(rstd_ PRIVATE ${RSTD_PRIVATE_SOURCES} mod.cpp)

add_library(rstd INTERFACE)
add_library(rstd::rstd ALIAS rstd)
target_link_libraries(rstd INTERFACE $<LINK_GROUP:RESCAN,rstd.core,rstd_>)
