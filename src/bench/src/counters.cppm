export module rstd.bench:counters;
import :model;
#if defined(__linux__)
import rstd.sys.libc;
namespace libc = rstd::sys::libc;
#endif

using namespace rstd::prelude;

namespace rstd::bench
{

void merge_counters(CounterSet& total, const CounterSet& part) noexcept {
    auto merge = [](Option<u64>& target, const Option<u64>& value) {
        if (target.is_some() && value.is_some())
            *target = target->saturating_add(*value);
        else
            target = None();
    };
    merge(total.page_faults, part.page_faults);
    merge(total.cpu_cycles, part.cpu_cycles);
    merge(total.context_switches, part.context_switches);
    merge(total.instructions, part.instructions);
    merge(total.branch_instructions, part.branch_instructions);
    merge(total.branch_misses, part.branch_misses);
}

class CounterBackend {
    static constexpr rstd::size_t COUNTER_COUNT = 6;

    CounterMode                     mode_;
    bool                            available_ {};
    bool                            calibrated_ {};
    i32                             error_code_ {};
    u32                             mask_ {};
    rstd::array<u64, COUNTER_COUNT> overhead_ {};
    u64                             instruction_loop_overhead_ {};

#if defined(__linux__)
    rstd::array<int, COUNTER_COUNT> descriptors_ { -1, -1, -1, -1, -1, -1 };

    struct GroupRead {
        rstd::uint64_t count;
        rstd::uint64_t time_enabled;
        rstd::uint64_t time_running;
        rstd::uint64_t values[COUNTER_COUNT];
    };

    static auto
    open_counter(rstd::uint32_t type, rstd::uint64_t config, int group_descriptor) noexcept -> int {
        auto attributes           = libc::perf_event_attr {};
        attributes.type           = type;
        attributes.size           = sizeof(attributes);
        attributes.config         = config;
        attributes.disabled       = group_descriptor == -1;
        attributes.exclude_kernel = 1;
        attributes.exclude_hv     = 1;
        attributes.read_format    = libc::PERF_FORMAT_GROUP | libc::PERF_FORMAT_TOTAL_TIME_ENABLED |
                                    libc::PERF_FORMAT_TOTAL_TIME_RUNNING;
        return static_cast<int>(
            libc::syscall(libc::SYS_perf_event_open, &attributes, 0, -1, group_descriptor, 0));
    }

    void close_all() noexcept {
        for (rstd::size_t index = 0; index < COUNTER_COUNT; ++index) {
            if (descriptors_[usize(index)] >= 0) {
                libc::close(descriptors_[usize(index)]);
                descriptors_[usize(index)] = -1;
            }
        }
    }

    void fail(int code) noexcept {
        error_code_ = i32(code);
        available_  = false;
        mask_       = u32();
        close_all();
    }

    void initialize() noexcept {
        if (mode_.is_Disabled()) return;

        struct CounterDescriptor {
            rstd::uint32_t type;
            rstd::uint64_t config;
        };
        constexpr rstd::array<CounterDescriptor, COUNTER_COUNT> descriptors {
            CounterDescriptor { libc::PERF_TYPE_HARDWARE, libc::PERF_COUNT_HW_CPU_CYCLES },
            CounterDescriptor { libc::PERF_TYPE_HARDWARE, libc::PERF_COUNT_HW_INSTRUCTIONS },
            CounterDescriptor { libc::PERF_TYPE_HARDWARE, libc::PERF_COUNT_HW_BRANCH_INSTRUCTIONS },
            CounterDescriptor { libc::PERF_TYPE_HARDWARE, libc::PERF_COUNT_HW_BRANCH_MISSES },
            CounterDescriptor { libc::PERF_TYPE_SOFTWARE, libc::PERF_COUNT_SW_PAGE_FAULTS },
            CounterDescriptor { libc::PERF_TYPE_SOFTWARE, libc::PERF_COUNT_SW_CONTEXT_SWITCHES },
        };

        for (rstd::size_t index = 0; index < COUNTER_COUNT; ++index) {
            descriptors_[usize(index)] = open_counter(descriptors[usize(index)].type,
                                                      descriptors[usize(index)].config,
                                                      index == 0 ? -1 : descriptors_[usize(0)]);
            if (descriptors_[usize(index)] < 0) {
                fail(libc::get_errno());
                return;
            }
        }
        available_ = true;
        mask_      = u32((1u << COUNTER_COUNT) - 1u);
    }

    auto read_group(rstd::array<u64, COUNTER_COUNT>& values) noexcept -> bool {
        auto       data  = GroupRead {};
        auto const bytes = libc::read(descriptors_[usize(0)], &data, sizeof(data));
        if (bytes != static_cast<libc::ssize_t>(sizeof(data)) || data.count != COUNTER_COUNT ||
            data.time_running == 0) {
            fail(bytes < 0 ? libc::get_errno() : libc::EIO);
            return false;
        }

        for (rstd::size_t index = 0; index < COUNTER_COUNT; ++index) {
            rstd::uint64_t value;
            [[unsafe]] { value = data.values[index]; }
            if (data.time_running != data.time_enabled) {
                auto scaled = static_cast<long double>(value) *
                              static_cast<long double>(data.time_enabled) /
                              static_cast<long double>(data.time_running);
                value       = scaled >= static_cast<long double>(u64::MAX.to_primitive())
                                  ? u64::MAX.to_primitive()
                                  : static_cast<rstd::uint64_t>(scaled);
            }
            values[usize(index)] = u64(value);
        }
        return true;
    }

    static auto mix(rstd::uint32_t value) noexcept -> rstd::uint32_t {
        value ^= value << 13u;
        value ^= value >> 17u;
        value ^= value << 5u;
        return value;
    }

    auto measure_loop(bool twice, rstd::array<u64, COUNTER_COUNT>& values) noexcept -> bool {
        constexpr rstd::uint64_t ITERATIONS = 100'003;
        auto                     remaining  = ITERATIONS;
        auto                     value      = rstd::uint32_t(1'234'567);
        begin_measure();
        if (! available_) return false;
        while (remaining-- > 0) {
            value = mix(value);
            if (twice) value = mix(value);
        }
        rstd::hint::black_box(value);
        if (libc::ioctl(descriptors_[usize(0)],
                        libc::PERF_EVENT_IOC_DISABLE,
                        libc::PERF_IOC_FLAG_GROUP) != 0) {
            fail(libc::get_errno());
            return false;
        }
        return read_group(values);
    }

    template<typename Op>
    void calibrate(Op&& op) noexcept {
        for (rstd::size_t index = 0; index < COUNTER_COUNT; ++index) {
            overhead_[usize(index)] = u64::MAX;
        }
        for (rstd::size_t sample = 0; sample < 100 && available_; ++sample) {
            begin_measure();
            op();
            if (libc::ioctl(descriptors_[usize(0)],
                            libc::PERF_EVENT_IOC_DISABLE,
                            libc::PERF_IOC_FLAG_GROUP) != 0) {
                fail(libc::get_errno());
                return;
            }
            rstd::array<u64, COUNTER_COUNT> values {};
            if (! read_group(values)) return;
            for (rstd::size_t index = 0; index < COUNTER_COUNT; ++index) {
                if (values[usize(index)] < overhead_[usize(index)])
                    overhead_[usize(index)] = values[usize(index)];
            }
        }
        if (! available_) return;

        rstd::array<u64, COUNTER_COUNT> once {};
        rstd::array<u64, COUNTER_COUNT> twice {};
        if (! measure_loop(false, once) || ! measure_loop(true, twice)) return;
        auto const once_instructions  = adjusted(once[usize(1)], 1);
        auto const twice_instructions = adjusted(twice[usize(1)], 1);
        auto       doubled            = u128(once_instructions.to_primitive()) * u128(2);
        auto       overhead           = doubled > u128(twice_instructions.to_primitive())
                                            ? doubled - u128(twice_instructions.to_primitive())
                                            : u128();
        instruction_loop_overhead_ =
            u64(((overhead + u128(50'001)) / u128(100'003)).to_primitive());
        calibrated_ = true;
    }
#else
    void initialize() noexcept {
        if (! mode_.is_Disabled()) error_code_ = i32(-1);
    }
#endif

    auto adjusted(u64 value, rstd::size_t index) const noexcept -> u64 {
        return value.saturating_sub(overhead_[usize(index)]);
    }

public:
    explicit CounterBackend(const CounterMode& mode): mode_(mode) { initialize(); }

    CounterBackend(const CounterBackend&)                    = delete;
    auto operator=(const CounterBackend&) -> CounterBackend& = delete;

    CounterBackend(CounterBackend&& other) noexcept
        : mode_(rstd::move(other.mode_)),
          available_(other.available_),
          calibrated_(other.calibrated_),
          error_code_(other.error_code_),
          mask_(other.mask_),
          instruction_loop_overhead_(other.instruction_loop_overhead_) {
        for (rstd::size_t index = 0; index < COUNTER_COUNT; ++index) {
            overhead_[usize(index)] = other.overhead_[usize(index)];
#if defined(__linux__)
            descriptors_[usize(index)]       = other.descriptors_[usize(index)];
            other.descriptors_[usize(index)] = -1;
#endif
        }
        other.available_ = false;
    }

    ~CounterBackend() {
#if defined(__linux__)
        close_all();
#endif
    }

    auto availability() const -> CounterAvailability {
        if (mode_.is_Disabled()) return CounterAvailability::Disabled();
        if (available_) return CounterAvailability::Available(mask_);
        return CounterAvailability::Unavailable(error_code_);
    }

    template<typename Op>
    void calibrate_measurement(Op&& op) noexcept {
#if defined(__linux__)
        if (available_ && ! calibrated_) calibrate(rstd::forward<Op>(op));
#else
        static_cast<void>(op);
#endif
    }

    void begin_measure() noexcept {
#if defined(__linux__)
        if (! available_) return;
        if (libc::ioctl(descriptors_[usize(0)],
                        libc::PERF_EVENT_IOC_RESET,
                        libc::PERF_IOC_FLAG_GROUP) != 0 ||
            libc::ioctl(descriptors_[usize(0)],
                        libc::PERF_EVENT_IOC_ENABLE,
                        libc::PERF_IOC_FLAG_GROUP) != 0) {
            fail(libc::get_errno());
        }
#endif
    }

    auto end_measure(u64 iterations) noexcept -> Result<CounterSet, i32> {
#if defined(__linux__)
        if (! available_) {
            if (mode_.is_Disabled()) return Ok(CounterSet {});
            return Err(error_code_);
        }
        if (libc::ioctl(descriptors_[usize(0)],
                        libc::PERF_EVENT_IOC_DISABLE,
                        libc::PERF_IOC_FLAG_GROUP) != 0) {
            auto code = libc::get_errno();
            fail(code);
            return Err(i32(code));
        }
        rstd::array<u64, COUNTER_COUNT> values {};
        if (! read_group(values)) return Err(error_code_);
        auto instructions =
            adjusted(values[usize(1)], 1)
                .saturating_sub(instruction_loop_overhead_.saturating_mul(iterations));
        return Ok(CounterSet {
            .page_faults         = Some(adjusted(values[usize(4)], 4)),
            .cpu_cycles          = Some(adjusted(values[usize(0)], 0)),
            .context_switches    = Some(adjusted(values[usize(5)], 5)),
            .instructions        = Some(instructions),
            .branch_instructions = Some(adjusted(values[usize(2)], 2)),
            .branch_misses       = Some(adjusted(values[usize(3)], 3)),
        });
#else
        if (mode_.is_Disabled()) return Ok(CounterSet {});
        return Err(error_code_);
#endif
    }
};

} // namespace rstd::bench
