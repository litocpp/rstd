add_library(rstd.cppstd STATIC)
target_compile_features(rstd.cppstd PUBLIC cxx_std_20)
get_target_property(cppstd_import_std rstd.cppstd CXX_MODULE_STD)
if(cppstd_import_std AND CMAKE_CXX_STANDARD IN_LIST CMAKE_CXX_COMPILER_IMPORT_STD)
  target_compile_definitions(rstd.cppstd PRIVATE RSTD_CPPSTD_IMPORT_STD=1)
endif()
target_sources(
  rstd.cppstd
  PUBLIC FILE_SET
         modules
         TYPE
         CXX_MODULES
         FILES
         rstd.cppm
         cppstd.cppm
         iter.cppm)

target_link_libraries(rstd.cppstd PUBLIC rstd.std)
