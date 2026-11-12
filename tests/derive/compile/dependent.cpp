import rstd.core;

template<typename T>
struct [[pmacro::derive("rstd::Debug")]] Dependent {
    T value;
};
