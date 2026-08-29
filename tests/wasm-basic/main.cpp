import rstd.alloc;
import rstd;

using namespace rstd::prelude;
using namespace rstd::literals;
using ::alloc::string::String;
using ::alloc::vec::Vec;

extern "C" void* __rstd_alloc(rstd::size_t size, rstd::size_t align);
extern "C" void  __rstd_dealloc(void* ptr, rstd::size_t size, rstd::size_t align);
extern "C" void* __rstd_realloc(void*          ptr,
                                 rstd::size_t old_size,
                                 rstd::size_t align,
                                 rstd::size_t new_size);

extern "C" [[clang::export_name("rstd_wasm_basic")]]
auto rstd_wasm_basic() -> rstd::uint32_t {
    auto values = Vec<u8>::make();
    values.push(u8(7));
    values.push(u8(11));
    auto text = String::make("wasm"_str);
    return rstd::uint32_t(values[usize {}].get().to_primitive() +
                          values[usize(1)].get().to_primitive() + text.len().to_primitive());
}

extern "C" [[clang::export_name("rstd_wasm_fs_unsupported")]]
auto rstd_wasm_fs_unsupported() -> rstd::uint32_t {
    auto path   = rstd::path::PathBuf::from("missing"_str);
    auto opened = rstd::fs::File::open(path.as_path());
    return rstd::uint32_t(opened.is_err() &&
                          opened.unwrap_err().kind() ==
                              rstd::io::error::ErrorKind {
                              rstd::io::error::ErrorKind::Unsupported });
}

extern "C" [[clang::export_name("rstd_wasm_allocator_reuse")]]
auto rstd_wasm_allocator_reuse() -> rstd::uint32_t {
    auto total = rstd::uint32_t {};
    for (auto iteration = rstd::uint32_t {}; iteration < 4; ++iteration) {
        auto values = Vec<u8>::with_capacity(usize(32));
        values.push(u8(iteration + 1));
        total += values[usize {}].get().to_primitive();
    }
    return total;
}

extern "C" [[clang::export_name("rstd_wasm_collections")]]
auto rstd_wasm_collections() -> rstd::uint32_t {
    auto values = rstd::collections::HashMap<i32, i32>::make();
    values.insert(i32(7), i32(11));
    values.insert(i32(13), i32(17));
    return rstd::uint32_t((**values.get(i32(7)) + **values.get(i32(13))).to_primitive());
}

extern "C" [[clang::export_name("rstd_wasm_allocator_contract")]]
auto rstd_wasm_allocator_contract() -> rstd::uint32_t {
    constexpr auto block_size = rstd::size_t(4096);
    auto*          first      = __rstd_alloc(block_size, rstd::size_t(16));
    auto*          second     = __rstd_alloc(block_size, rstd::size_t(16));
    if (first == nullptr || second == nullptr || first == second) return rstd::uint32_t {};
    __rstd_dealloc(first, block_size, rstd::size_t(16));
    __rstd_dealloc(second, block_size, rstd::size_t(16));
    auto* merged = __rstd_alloc(rstd::size_t(7000), rstd::size_t(16));
    if (merged != first) return rstd::uint32_t {};
    __rstd_dealloc(merged, rstd::size_t(7000), rstd::size_t(16));

    auto* aligned = __rstd_alloc(rstd::size_t(17), rstd::size_t(256));
    if (aligned == nullptr || reinterpret_cast<rstd::size_t>(aligned) % rstd::size_t(256) != 0) {
        return rstd::uint32_t {};
    }
    __rstd_dealloc(aligned, rstd::size_t(17), rstd::size_t(256));

    auto* original = static_cast<rstd::uint8_t*>(
        __rstd_alloc(rstd::size_t(64), rstd::size_t(16)));
    if (original == nullptr) return rstd::uint32_t {};
    for (auto index = rstd::size_t {}; index < rstd::size_t(64); ++index) {
        original[index] = static_cast<rstd::uint8_t>(index);
    }
    auto* resized = static_cast<rstd::uint8_t*>(__rstd_realloc(
        original, rstd::size_t(64), rstd::size_t(16), rstd::size_t(256)));
    if (resized == nullptr) return rstd::uint32_t {};
    for (auto index = rstd::size_t {}; index < rstd::size_t(64); ++index) {
        if (resized[index] != static_cast<rstd::uint8_t>(index)) return rstd::uint32_t {};
    }
    __rstd_dealloc(resized, rstd::size_t(256), rstd::size_t(16));

    auto pages = __builtin_wasm_memory_size(0);
    auto* grown = __rstd_alloc(rstd::size_t(3 * 65536), rstd::size_t(16));
    if (grown == nullptr || __builtin_wasm_memory_size(0) <= pages) return rstd::uint32_t {};
    __rstd_dealloc(grown, rstd::size_t(3 * 65536), rstd::size_t(16));

    return rstd::uint32_t(
        __rstd_alloc(static_cast<rstd::size_t>(-1), rstd::size_t(16)) == nullptr);
}
