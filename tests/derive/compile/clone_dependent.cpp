import rstd.core;

template<typename T>
struct [[pmacro::derive("rstd::Clone")]] Record {
    T value;
};
