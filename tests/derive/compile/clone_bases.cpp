import rstd.core;

struct Base {};
struct [[pmacro::derive("rstd::Clone")]] Record : Base {};
