#include "benchmark.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifndef RSTD_BENCH_BUILD_TYPE
#define RSTD_BENCH_BUILD_TYPE "unknown"
#endif

#ifndef RSTD_BENCH_ASAN
#define RSTD_BENCH_ASAN 0
#endif

namespace
{

struct Options {
    const char*   m_suite { "all" };
    const char*   m_json_path { nullptr };
    std::uint64_t m_iterations { 0 };
    bool          m_quick { false };
    bool          m_list { false };
};

struct RunResult {
    const rstd_bench::BenchCase* m_case {};
    std::uint64_t                m_iterations {};
    std::uint64_t                m_elapsed_ns {};
    std::uint64_t                m_items {};
    std::uint64_t                m_bytes {};
    bool                         m_ok {};
};

auto parse_u64(const char* value) -> std::uint64_t {
    return static_cast<std::uint64_t>(std::strtoull(value, nullptr, 10));
}

auto parse_options(int argc, char** argv) -> Options {
    auto options = Options {};

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--suite") == 0 && i + 1 < argc) {
            options.m_suite = argv[++i];
        } else if (std::strcmp(argv[i], "--iterations") == 0 && i + 1 < argc) {
            options.m_iterations = parse_u64(argv[++i]);
        } else if (std::strcmp(argv[i], "--json") == 0 && i + 1 < argc) {
            options.m_json_path = argv[++i];
        } else if (std::strcmp(argv[i], "--quick") == 0) {
            options.m_quick = true;
        } else if (std::strcmp(argv[i], "--list") == 0) {
            options.m_list = true;
        } else if (std::strcmp(argv[i], "--help") == 0) {
            std::printf("usage: rstd_bench [--suite all|alloc|sync|async|net] [--quick] [--iterations N] [--json PATH] [--list]\n");
            std::exit(0);
        }
    }

    return options;
}

auto suite_matches(const Options& options, const rstd_bench::BenchCase& bench) -> bool {
    return std::strcmp(options.m_suite, "all") == 0 ||
           std::strcmp(options.m_suite, bench.m_suite) == 0;
}

auto selected_iterations(const Options& options, const rstd_bench::BenchCase& bench)
    -> std::uint64_t {
    if (options.m_iterations != 0) {
        return options.m_iterations;
    }
    return options.m_quick ? bench.m_quick_iterations : bench.m_iterations;
}

auto run_case(const Options& options, const rstd_bench::BenchCase& bench) -> RunResult {
    auto context = rstd_bench::BenchContext {
        .m_iterations = selected_iterations(options, bench),
        .m_quick      = options.m_quick,
    };

    auto started = std::chrono::steady_clock::now();
    bool ok      = bench.m_run(context);
    auto ended   = std::chrono::steady_clock::now();

    auto elapsed =
        std::chrono::duration_cast<std::chrono::nanoseconds>(ended - started).count();

    return RunResult {
        .m_case       = &bench,
        .m_iterations = context.m_iterations,
        .m_elapsed_ns = static_cast<std::uint64_t>(elapsed),
        .m_items      = context.m_items_processed,
        .m_bytes      = context.m_bytes_processed,
        .m_ok         = ok,
    };
}

void print_result(const RunResult& result) {
    double ns_per_iter = result.m_iterations == 0
                           ? 0.0
                           : static_cast<double>(result.m_elapsed_ns) /
                               static_cast<double>(result.m_iterations);
    double total_ms = static_cast<double>(result.m_elapsed_ns) / 1'000'000.0;

    std::printf("%-8s %-32s %10llu %12.2f ns/op %10.3f ms %s\n",
                result.m_case->m_suite,
                result.m_case->m_name,
                static_cast<unsigned long long>(result.m_iterations),
                ns_per_iter,
                total_ms,
                result.m_ok ? "ok" : "failed");
}

void write_json(FILE* file, const RunResult* results, std::size_t count) {
    std::fprintf(file, "[\n");
    for (std::size_t i = 0; i < count; ++i) {
        const auto& result = results[i];
        double ns_per_iter = result.m_iterations == 0
                               ? 0.0
                               : static_cast<double>(result.m_elapsed_ns) /
                                   static_cast<double>(result.m_iterations);
        std::fprintf(file,
                     "  {\"suite\":\"%s\",\"name\":\"%s\",\"iterations\":%llu,"
                     "\"elapsed_ns\":%llu,\"ns_per_iter\":%.3f,"
                     "\"items\":%llu,\"bytes\":%llu,\"build_type\":\"%s\","
                     "\"asan\":%s,\"ok\":%s}%s\n",
                     result.m_case->m_suite,
                     result.m_case->m_name,
                     static_cast<unsigned long long>(result.m_iterations),
                     static_cast<unsigned long long>(result.m_elapsed_ns),
                     ns_per_iter,
                     static_cast<unsigned long long>(result.m_items),
                     static_cast<unsigned long long>(result.m_bytes),
                     RSTD_BENCH_BUILD_TYPE,
                     RSTD_BENCH_ASAN ? "true" : "false",
                     result.m_ok ? "true" : "false",
                     i + 1 == count ? "" : ",");
    }
    std::fprintf(file, "]\n");
}

template<std::size_t N>
void append_list(rstd_bench::BenchCase const* (&cases)[N],
                 std::size_t (&lens)[N],
                 std::size_t index,
                 rstd_bench::BenchList list) {
    cases[index] = list.m_cases;
    lens[index]  = list.m_len;
}

} // namespace

auto main(int argc, char** argv) -> int {
    auto options = parse_options(argc, argv);

    rstd_bench::BenchCase const* suites[4] {};
    std::size_t                  lens[4] {};
    append_list(suites, lens, 0, rstd_bench::alloc_benchmarks());
    append_list(suites, lens, 1, rstd_bench::sync_benchmarks());
    append_list(suites, lens, 2, rstd_bench::async_benchmarks());
    append_list(suites, lens, 3, rstd_bench::net_benchmarks());

    if (options.m_list) {
        for (std::size_t i = 0; i < 4; ++i) {
            for (std::size_t j = 0; j < lens[i]; ++j) {
                if (suite_matches(options, suites[i][j])) {
                    std::printf("%s.%s\n", suites[i][j].m_suite, suites[i][j].m_name);
                }
            }
        }
        return 0;
    }

    RunResult results[128] {};
    std::size_t result_count = 0;
    bool all_ok = true;

    std::printf("%-8s %-32s %10s %20s %13s %s\n",
                "suite", "name", "iters", "time", "total", "status");
    std::printf("build=%s asan=%s\n",
                RSTD_BENCH_BUILD_TYPE,
                RSTD_BENCH_ASAN ? "true" : "false");

    for (std::size_t i = 0; i < 4; ++i) {
        for (std::size_t j = 0; j < lens[i]; ++j) {
            const auto& bench = suites[i][j];
            if (! suite_matches(options, bench)) {
                continue;
            }
            auto result = run_case(options, bench);
            all_ok = all_ok && result.m_ok;
            print_result(result);
            results[result_count++] = result;
        }
    }

    if (options.m_json_path != nullptr) {
        FILE* file = std::fopen(options.m_json_path, "w");
        if (file == nullptr) {
            std::fprintf(stderr, "failed to open json output: %s\n", options.m_json_path);
            return 1;
        }
        write_json(file, results, result_count);
        std::fclose(file);
    }

    if (result_count == 0) {
        std::fprintf(stderr, "no benchmark cases selected\n");
        return 1;
    }

    return all_ok ? 0 : 1;
}
