add_subdirectory(basic)
add_subdirectory(core)
add_subdirectory(alloc)
add_subdirectory(std)
add_subdirectory(nostd)

add_library(rstd INTERFACE)
add_library(rstd::rstd ALIAS rstd)
target_link_libraries(rstd INTERFACE $<LINK_GROUP:RESCAN,rstd.core,rstd.alloc,rstd.std>)

add_library(rstd.nostd_ INTERFACE)
add_library(rstd::nostd ALIAS rstd.nostd_)
target_link_libraries(rstd.nostd_ INTERFACE $<LINK_GROUP:RESCAN,rstd.core,rstd.alloc,rstd.std> rstd.nostd)
