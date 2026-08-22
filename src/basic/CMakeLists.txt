add_library(rstd.basic STATIC)
target_compile_features(rstd.basic PUBLIC cxx_std_20)
if(RSTD_USE_ASAN)
  target_link_libraries(rstd.basic PUBLIC "$<BUILD_INTERFACE:rstd.asan>")
endif()
if(RSTD_COMPILER_RT_LIBRARY)
  target_link_libraries(
    rstd.basic
    PUBLIC "$<BUILD_INTERFACE:${RSTD_COMPILER_RT_PATH}>"
           "$<INSTALL_INTERFACE:${RSTD_COMPILER_RT_LIBRARY}.lib>")
endif()
target_sources(
  rstd.basic
  PUBLIC FILE_SET
         modules
         TYPE
         CXX_MODULES
         FILES
         src/lib.cppm
         src/std.cppm
         src/source_location.cppm
         src/prelude.cppm
         src/basic.cppm
         src/mtp/mod.cppm
         src/tuple.cppm
         src/panic_info.cppm)
