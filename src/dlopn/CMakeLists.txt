add_library(rstd.dlopn STATIC)
add_library(rstd::dlopn ALIAS rstd.dlopn)

target_compile_features(rstd.dlopn PUBLIC cxx_std_20)
target_sources(
  rstd.dlopn
  PUBLIC FILE_SET modules TYPE CXX_MODULES FILES mod.cppm
  PRIVATE dlopn.cpp)
target_link_libraries(rstd.dlopn PUBLIC rstd::rstd PRIVATE ${CMAKE_DL_LIBS})

set_target_properties(rstd.dlopn PROPERTIES CXX_SCAN_FOR_MODULES ON)
