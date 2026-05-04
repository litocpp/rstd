add_library(rstd.log STATIC)
add_library(rstd::log ALIAS rstd.log)
target_compile_features(rstd.log PUBLIC cxx_std_20)
target_link_libraries(rstd.log PUBLIC rstd.core rstd.std)

target_sources(
  rstd.log
  PUBLIC FILE_SET
         modules
         TYPE
         CXX_MODULES
         FILES
         mod.cppm
         level.cppm
         record.cppm
         logger.cppm
         macros.cppm
         env_logger.cppm)
