import rstd.core;
import rstd.alloc;

struct NoDebug {};

struct [[pmacro::derive("rstd::Debug")]] FieldWithoutDebug {
    NoDebug value;
};

int main() {
    (void)rstd::format("{:?}", FieldWithoutDebug {});
}
