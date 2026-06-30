#pragma once

#include <cstdint>
#include <cstddef>

namespace rstd_bench
{

struct BenchContext {
    std::uint64_t m_iterations;
    std::uint64_t m_items_processed { 0 };
    std::uint64_t m_bytes_processed { 0 };
    bool          m_quick { false };

    auto iterations() const noexcept -> std::uint64_t { return m_iterations; }

    void set_items_processed(std::uint64_t count) noexcept { m_items_processed = count; }

    void set_bytes_processed(std::uint64_t count) noexcept { m_bytes_processed = count; }
};

using BenchFn = bool (*)(BenchContext&);

struct BenchCase {
    const char*   m_suite;
    const char*   m_name;
    std::uint64_t m_iterations;
    std::uint64_t m_quick_iterations;
    BenchFn       m_run;
};

struct BenchList {
    const BenchCase* m_cases;
    std::size_t      m_len;
};

BenchList alloc_benchmarks();
BenchList sync_benchmarks();
BenchList async_benchmarks();
BenchList net_benchmarks();

} // namespace rstd_bench
