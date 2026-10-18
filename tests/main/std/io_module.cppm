export module rstd.tests.std_io_module;
import rstd.io;

using rstd::mem::MaybeUninit;

using namespace rstd;
using namespace rstd::literals;

export auto std_io_module() -> bool {
    rstd::array<MaybeUninit<byte>, 3> uninitialized;
    auto target = io::BorrowedBuf::from_uninit(uninitialized.as_mut_slice());
    auto direct = io::Cursor("xyz"_bytes);
    if (io::read_buf_exact(direct, target.unfilled()).is_err() || target.filled() != "xyz"_bytes)
        return false;
    auto cursor = io::Cursor(slice<u8>("abc"_bytes));
    auto reader = io::BufReader(rstd::move(cursor), usize(2));
    byte output[3] {};
    auto read = io::read_exact(reader, mut_ref<u8[]>::from_raw_parts(output, usize(3)));
    if (read.is_err() || output[0] != byte('a') || output[2] != byte('c')) return false;
    using Cursor = io::Cursor<slice<u8>>;
    auto custom =
        io::BufReader<Cursor>::try_make_in(Cursor("abc"_bytes), ::alloc::Global {}, usize(2));
    if (custom.is_err()) return false;
    auto handle   = io::BasicReadSeekHandle<>::make(custom.unwrap());
    auto bytes    = ::alloc::vec::Vec<u8>::make();
    auto borrowed = *handle;
    if (io::read_to_end(borrowed, bytes).unwrap() != usize(3)) return false;
    auto buffered = io::BufWriter<io::Sink>::try_make_in(io::sink(), ::alloc::Global {});
    if (buffered.is_err()) return false;
    auto sink = buffered.unwrap();
    if (io::write_all(sink, "abc"_bytes).is_err()) return false;
    static_assert(mtp::same_as<io::ReadSeekHandle, io::BasicReadSeekHandle<>>);
    static_assert(mtp::same_as<io::WriteSeekHandle, io::BasicWriteSeekHandle<>>);
    static_assert(mtp::same_as<io::ReadWriteSeekHandle, io::BasicReadWriteSeekHandle<>>);
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
