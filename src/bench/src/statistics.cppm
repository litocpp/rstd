export module rstd.bench:statistics;
export import :model;

using namespace rstd::prelude;

auto bench_duration_ns(rstd::time::Duration value) noexcept -> f64 {
    return f64(value.as_secs_f64() * 1'000'000'000.0);
}

auto bench_saturating_add(u64 left, u64 right) noexcept -> u64 {
    auto result = left.checked_add(right);
    return result.is_some() ? rstd::move(result).unwrap_unchecked() : u64::MAX;
}

auto bench_saturating_mul(u64 left, u64 right) noexcept -> u64 {
    auto result = left.checked_mul(right);
    return result.is_some() ? rstd::move(result).unwrap_unchecked() : u64::MAX;
}

auto bench_sorted_median(slice<f64> values) noexcept -> f64 {
    auto const length = values.len().to_primitive();
    if (length == 0) return f64();
    auto const middle = length / 2;
    if ((length & 1u) != 0) return values[usize(middle)];
    return (values[usize(middle - 1)] + values[usize(middle)]) / f64(2.0);
}

namespace rstd::bench
{

export auto median(Vec<f64> values) noexcept -> f64 {
    slice_::sort_unstable_by(values.as_mut_slice().as_mut_ref(), [](f64 left, f64 right) {
        return left.total_cmp(right) < 0;
    });
    return bench_sorted_median(values.as_slice());
}

export auto percentile(Vec<f64> values, f64 percentile_value) noexcept -> f64 {
    if (values.is_empty()) return f64();
    slice_::sort_unstable_by(values.as_mut_slice().as_mut_ref(), [](f64 left, f64 right) {
        return left.total_cmp(right) < 0;
    });
    auto clamped = percentile_value.clamp(f64(), f64(1.0));
    auto index   = static_cast<rstd::size_t>(
        __builtin_ceil(clamped.to_primitive() * static_cast<double>(values.len().to_primitive())));
    if (index == 0) return values[usize()];
    if (index > values.len().to_primitive()) index = values.len().to_primitive();
    return values[usize(index - 1)];
}

auto BenchmarkResult::summary() const -> BenchmarkSummary {
    auto per_unit         = Vec<f64>::with_capacity(measurements_.len());
    auto total_elapsed    = time::Duration_ZERO;
    auto total_iterations = u64();

    f64 sum;
    f64 minimum = f64::INFINITY_;
    f64 maximum;

    f64  instructions;
    f64  cycles;
    f64  branches;
    f64  branch_misses;
    bool has_instructions  = false;
    bool has_cycles        = false;
    bool has_branches      = false;
    bool has_branch_misses = false;

    for (const auto& measurement : measurements_) {
        auto value = bench_duration_ns(measurement.elapsed) /
                     f64(static_cast<double>(measurement.iterations.to_primitive())) /
                     run_config_.batch;
        per_unit.push(rstd::move(value));
        sum += value;
        minimum = minimum.min(value);
        maximum = maximum.max(value);
        total_elapsed += measurement.elapsed;
        total_iterations = bench_saturating_add(total_iterations, measurement.iterations);

        if (measurement.counters.instructions.is_some()) {
            has_instructions = true;
            instructions +=
                f64(static_cast<double>(measurement.counters.instructions->to_primitive()));
        }
        if (measurement.counters.cpu_cycles.is_some()) {
            has_cycles = true;
            cycles += f64(static_cast<double>(measurement.counters.cpu_cycles->to_primitive()));
        }
        if (measurement.counters.branch_instructions.is_some()) {
            has_branches = true;
            branches +=
                f64(static_cast<double>(measurement.counters.branch_instructions->to_primitive()));
        }
        if (measurement.counters.branch_misses.is_some()) {
            has_branch_misses = true;
            branch_misses +=
                f64(static_cast<double>(measurement.counters.branch_misses->to_primitive()));
        }
    }

    auto median_values = Vec<f64>::with_capacity(per_unit.len());
    for (auto value : per_unit) median_values.push(f64(value.to_primitive()));
    auto median_value = median(rstd::move(median_values));

    auto errors = Vec<f64>::with_capacity(per_unit.len());
    for (auto value : per_unit) {
        if (value == f64()) {
            errors.push(f64());
        } else {
            errors.push(((value - median_value) / value).abs());
        }
    }

    auto const sample_count = static_cast<double>(measurements_.len().to_primitive());
    auto const mean         = measurements_.is_empty() ? f64() : sum / f64(sample_count);
    auto const total_units =
        f64(static_cast<double>(total_iterations.to_primitive())) * run_config_.batch;
    auto per_counter_unit = [&](f64 value) -> Option<f64> {
        if (total_units == f64()) return None();
        return Some(value / total_units);
    };

    return BenchmarkSummary {
        .median_ns_per_unit               = median_value,
        .mean_ns_per_unit                 = mean,
        .minimum_ns_per_unit              = measurements_.is_empty() ? f64() : minimum,
        .maximum_ns_per_unit              = maximum,
        .median_absolute_percentage_error = median(rstd::move(errors)),
        .units_per_second = median_value == f64() ? f64() : f64(1'000'000'000.0) / median_value,
        .total_elapsed    = total_elapsed,
        .total_iterations = total_iterations,
        .total_items      = bench_saturating_mul(total_iterations, run_config_.items_per_iteration),
        .total_bytes      = bench_saturating_mul(total_iterations, run_config_.bytes_per_iteration),
        .instructions_per_unit  = has_instructions ? per_counter_unit(instructions) : None(),
        .cycles_per_unit        = has_cycles ? per_counter_unit(cycles) : None(),
        .instructions_per_cycle = has_instructions && has_cycles && cycles != f64()
                                      ? Some(instructions / cycles)
                                      : None(),
        .branches_per_unit      = has_branches ? per_counter_unit(branches) : None(),
        .branch_miss_ratio      = has_branches && has_branch_misses && branches != f64()
                                      ? Some(branch_misses / branches)
                                      : None(),
    };
}

} // namespace rstd::bench
