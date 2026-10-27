add_library(rstd.runtime STATIC src/alloc.c src/panic.c src/math.c src/memory.c)
target_compile_features(rstd.runtime PRIVATE c_std_17)
target_include_directories(rstd.runtime PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)

if(CMAKE_C_COMPILER_TARGET STREQUAL "wasm32-unknown-unknown")
  target_compile_options(rstd.runtime PRIVATE -ffreestanding -fno-builtin)
  target_sources(rstd.runtime PRIVATE src/alloc_wasm.c)
  source_group("dlmalloc" FILES src/alloc_wasm.c)
  set(RSTD_WASM_BUILTINS
    compiler-rt/builtins/multi3.c
    compiler-rt/builtins/udivti3.c
    compiler-rt/builtins/udivmodti4.c
    compiler-rt/builtins/divti3.c
    compiler-rt/builtins/modti3.c
    compiler-rt/builtins/umodti3.c
    compiler-rt/builtins/ashlti3.c
    compiler-rt/builtins/ashrti3.c
    compiler-rt/builtins/lshrti3.c)
  target_sources(rstd.runtime PRIVATE ${RSTD_WASM_BUILTINS})
  source_group("compiler-rt" FILES ${RSTD_WASM_BUILTINS})
endif()
