add_library(rstd.alloc STATIC)
target_compile_features(rstd.alloc PUBLIC cxx_std_20)
target_link_libraries(rstd.alloc PUBLIC rstd.core PRIVATE rstd.runtime)
target_sources(
  rstd.alloc
  PUBLIC FILE_SET
         modules
         TYPE
         CXX_MODULES
         FILES
         mod.cppm
         alloc.cppm
         rc.cppm
         string.cppm
         str.cppm
         boxed.cppm
         sync.cppm
         ffi/mod.cppm
         ffi/c_str.cppm
         vec/mod.cppm
         collections/mod.cppm
         collections/btree/node.cppm
         collections/btree/map.cppm
         collections/btree/set.cppm
         collections/hash/table.cppm
         collections/hash/map.cppm
         collections/hash/set.cppm
         hash/random.cppm)
