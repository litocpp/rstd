add_library(rstd.nostd STATIC new.cpp cxa.cpp)
target_compile_features(rstd.nostd PUBLIC cxx_std_20)