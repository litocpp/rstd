add_subdirectory(basic)
add_subdirectory(runtime)
add_subdirectory(core)
add_subdirectory(alloc)
add_subdirectory(std)
if(UNIX)
  add_subdirectory(dlopn)
endif()
add_subdirectory(json)
add_subdirectory(toml)
add_subdirectory(log)
add_subdirectory(argparse)
add_subdirectory(bench)
add_subdirectory(nostd)

add_library(rstd::rstd ALIAS rstd.std)

add_library(rstd.nostd_ INTERFACE)
add_library(rstd::nostd ALIAS rstd.nostd_)

target_link_libraries(rstd.nostd_ INTERFACE rstd.std rstd.alloc rstd.core rstd.runtime rstd.nostd)

add_subdirectory(cppstd)
