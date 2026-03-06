set(RSTD_PRIVATE_SOURCES)
set(RSTD_SYS_SOURCES)
set(RSTD_IO_SOURCES)
set(RSTD_THREAD_SOURCES)
set(RSTD_SYNC_SOURCES)

add_subdirectory(sys)
add_subdirectory(io)
add_subdirectory(thread)
add_subdirectory(sync)

add_library(rstd.std)
target_compile_features(rstd.std PUBLIC cxx_std_23)
target_link_libraries(rstd.std PUBLIC rstd.core rstd.alloc)
set_target_properties(rstd.std PROPERTIES OUTPUT_NAME rstd)

target_sources(
  rstd.std
  PUBLIC FILE_SET
         modules
         TYPE
         CXX_MODULES
         FILES
         mod.cppm
         forward.cppm
         error.cppm
         panicking.cppm
         process.cppm
         ffi/mod.cppm
         ${RSTD_SYS_SOURCES}
         ${RSTD_IO_SOURCES}
         ${RSTD_THREAD_SOURCES}
         ${RSTD_SYNC_SOURCES})

target_sources(rstd.std PRIVATE ${RSTD_PRIVATE_SOURCES} mod.cpp)

add_library(rstd INTERFACE)
add_library(rstd::rstd ALIAS rstd)
target_link_libraries(rstd INTERFACE $<LINK_GROUP:RESCAN,rstd.core,rstd.alloc,rstd.std>)
