add_library(rstd.basic STATIC)
target_compile_features(rstd.basic PUBLIC cxx_std_20)
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
         mod.cppm
         prelude.cppm
         basic.cppm
         mtp.cppm
         mtp.std.cppm
         tuple.cppm
         panic_info.cppm)
