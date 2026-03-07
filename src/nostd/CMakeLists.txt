add_library(rstd.new STATIC new.cpp)
target_compile_features(rstd.new PUBLIC cxx_std_20)

add_library(rstd::new ALIAS rstd.new)