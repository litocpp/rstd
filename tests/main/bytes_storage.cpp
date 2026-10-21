import rstd;

using rstd::bytes::Bytes;

static constexpr unsigned char cross_tu_bytes[] { 'c', 'r', 'o', 's', 's' };

auto make_static_bytes_cross_tu() -> Bytes {
    return Bytes::from_static<cross_tu_bytes>();
}
auto clone_bytes_cross_tu(const Bytes& bytes) -> Bytes {
    return bytes.clone();
}
