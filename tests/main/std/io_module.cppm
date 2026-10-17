export module rstd.tests.std_io_module;
import rstd.io;

using namespace rstd;
using namespace rstd::literals;

export auto std_io_module() -> bool {
    auto cursor = io::Cursor(slice<u8>("abc"_bytes));
    auto reader = io::BufReader(rstd::move(cursor), usize(2));
    byte output[3] {};
    auto read = io::read_exact(reader, mut_ref<u8[]>::from_raw_parts(output, usize(3)));
    if (read.is_err() || output[0] != byte('a') || output[2] != byte('c')) return false;
    auto writer = io::Cursor(::alloc::vec::Vec<u8>::make());
    if (io::write_all(writer, "xyz"_bytes).is_err()) return false;
    if (writer.get_ref().len() != usize(3)) return false;
    {
        auto out = io::stdout().lock();
        if (as<io::Write>(out).flush().is_err()) return false;
    }
    {
        auto in = io::stdin().lock();
        (void)in;
    }
    return io::print_fmt({ "", 0, nullptr, 0 }).is_ok() &&
           io::eprint_fmt({ "", 0, nullptr, 0 }).is_ok();
}
