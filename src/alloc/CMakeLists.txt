add_library(rstd.alloc STATIC)
target_compile_features(rstd.alloc PUBLIC cxx_std_20)
target_link_libraries(rstd.alloc PUBLIC rstd.core PRIVATE rstd.runtime)
target_sources(rstd.alloc PRIVATE src/string.cpp)
target_sources(
  rstd.alloc
  PUBLIC FILE_SET
         modules
         TYPE
         CXX_MODULES
         FILES
         src/lib.cppm
         src/alloc.cppm
         src/rc.cppm
         src/string.cppm
         src/str.cppm
         src/boxed.cppm
         src/sync.cppm
         src/ffi/mod.cppm
         src/ffi/c_str.cppm
         src/vec.cppm
         src/collections/mod.cppm
         src/collections/btree_node.cppm
         src/collections/btree_map.cppm
         src/collections/btree_set.cppm
         src/collections/hash_table.cppm
         src/collections/hash_map.cppm
         src/collections/hash_set.cppm
         src/hash/random.cppm)
