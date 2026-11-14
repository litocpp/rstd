import rstd.core;

struct MoveOnly {
    MoveOnly(MoveOnly&&)                    = default;
    auto operator=(MoveOnly&&) -> MoveOnly& = default;
};

struct [[pmacro::derive("rstd::Clone")]] Record {
    MoveOnly resource;
};
