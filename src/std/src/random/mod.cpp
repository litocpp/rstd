module rstd;
import :random;
import :sys.random;

using namespace rstd::prelude;

auto rstd::random::SystemRng::fill_bytes(mut_ref<u8[]> output) -> io::Result<empty> {
    return sys::random::fill_bytes(output);
}

auto rstd::random::fill_secure(mut_ref<u8[]> output) -> io::Result<empty> {
    return SystemRng {}.fill_bytes(output);
}
