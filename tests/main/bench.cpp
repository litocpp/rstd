#include <rstd/test/gtest.hpp>

import rstd.bench;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace
{

struct FakeClock {
    mutable u64          now {};
    u64                  tick { u64(100) };
    rstd::time::Duration clock_resolution { rstd::time::Duration::from_nanos(u64(1)) };

    auto now_ns() const noexcept -> u64 {
        auto current = now;
        now += tick;
        return current;
    }

    auto resolution() const noexcept -> Result<rstd::time::Duration, rstd::bench::ClockError> {
        return Ok(clock_resolution);
    }
};

struct CountingClock {
    u64* operations;

    auto now_ns() const noexcept -> u64 { return *operations; }

    auto resolution() const noexcept -> Result<rstd::time::Duration, rstd::bench::ClockError> {
        return Ok(rstd::time::Duration::from_nanos(u64(1)));
    }
};

} // namespace

#if 0
TEST(Slice, SortUnstableUsesPublicSliceOwner) {
    int  values[] { 5, 1, 4, 2, 3 };
    auto slice = rstd::mut_ref<int[]>::from_raw_parts(values, usize(5));

    rstd::slice_::sort_unstable(slice);

    for (rstd::size_t index = 0; index < 5; ++index) {
        EXPECT_EQ(values[index], static_cast<int>(index + 1));
    }
}

TEST(BenchStatistics, MedianAndPercentileUseOwnedSamples) {
    auto values = Vec<f64>::with_capacity(usize(4));
    values.push(f64(4.0));
    values.push(f64(1.0));
    values.push(f64(3.0));
    values.push(f64(2.0));

    auto percentile_values = Vec<f64>::with_capacity(usize(4));
    for (auto value : values) percentile_values.push(f64(value.to_primitive()));

    EXPECT_DOUBLE_EQ(rstd::bench::median(rstd::move(values)).to_primitive(), 2.5);
    EXPECT_DOUBLE_EQ(
        rstd::bench::percentile(rstd::move(percentile_values), f64(0.95)).to_primitive(), 4.0);
}

TEST(Bench, ExactIterationsExcludeWarmupAndPreserveEpochs) {
    auto config                   = rstd::bench::BenchConfig {};
    config.epochs                 = usize(3);
    config.exact_epoch_iterations = Some(u64(4));
    config.warmup_iterations      = u64(2);
    config.counter_mode           = rstd::bench::CounterMode::Disabled();

    auto operations = u64();
    auto bench      = rstd::bench::BasicBench<FakeClock>(FakeClock {}, rstd::move(config));
    auto result     = bench.run("exact"_str, [&] {
        ++operations;
        rstd::hint::black_box(operations);
    });

    ASSERT_TRUE(result.is_ok());
    auto measured = rstd::move(result).unwrap_unchecked();
    EXPECT_EQ(operations, u64(14));
    EXPECT_EQ(measured.clock_resolution(), rstd::time::Duration::from_nanos(u64(1)));
    ASSERT_EQ(measured.measurements().len(), usize(3));
    for (usize index; index < measured.measurements().len(); ++index) {
        const auto& epoch = measured.measurements()[index];
        EXPECT_EQ(epoch.iterations, u64(4));
        EXPECT_EQ(epoch.elapsed, rstd::time::Duration::from_nanos(u64(100)));
    }

    auto summary = measured.summary();
    EXPECT_DOUBLE_EQ(summary.median_ns_per_unit.to_primitive(), 25.0);
    EXPECT_EQ(summary.total_iterations, u64(12));
}

TEST(Bench, AdaptiveIterationsReachTargetEpoch) {
    auto config           = rstd::bench::BenchConfig {};
    config.epochs         = usize(2);
    config.min_epoch_time = rstd::time::Duration::from_nanos(u64(10));
    config.max_epoch_time = rstd::time::Duration::from_nanos(u64(10));
    config.counter_mode   = rstd::bench::CounterMode::Disabled();
    config.jitter_seed    = u64(1);

    auto operations = u64();
    auto bench = rstd::bench::BasicBench<CountingClock>(CountingClock { .operations = &operations },
                                                        rstd::move(config));
    auto result = bench.run("adaptive"_str, [&] {
        ++operations;
        rstd::hint::black_box(operations);
    });

    ASSERT_TRUE(result.is_ok());
    auto measured = rstd::move(result).unwrap_unchecked();
    ASSERT_EQ(measured.measurements().len(), usize(2));
    EXPECT_GE(measured.measurements()[usize()].iterations, u64(10));
    EXPECT_GE(measured.measurements()[usize(1)].iterations, u64(10));
    EXPECT_LE(measured.measurements()[usize()].iterations, u64(12));
    EXPECT_LE(measured.measurements()[usize(1)].iterations, u64(12));
}

TEST(Bench, RejectsZeroMaximumEpochAndRepeatedZeroElapsed) {
    auto invalid           = rstd::bench::BenchConfig {};
    invalid.max_epoch_time = rstd::time::Duration_ZERO;
    EXPECT_TRUE(invalid.validate().unwrap_err().is_ZeroMaxEpochTime());

    auto config         = rstd::bench::BenchConfig {};
    config.counter_mode = rstd::bench::CounterMode::Disabled();
    auto clock          = FakeClock { .tick = u64() };
    auto bench          = rstd::bench::BasicBench<FakeClock>(rstd::move(clock), rstd::move(config));
    auto result         = bench.run("zero"_str, [] {
    });
    ASSERT_TRUE(result.is_err());
    EXPECT_TRUE(result.unwrap_err_unchecked().is_OperationOptimizedAway());
}

TEST(Bench, RequiredCountersReflectBackendAvailability) {
    auto config                   = rstd::bench::BenchConfig {};
    config.counter_mode           = rstd::bench::CounterMode::Required();
    config.exact_epoch_iterations = Some(u64(1));
    auto bench = rstd::bench::BasicBench<FakeClock>(FakeClock {}, rstd::move(config));

    auto result = bench.run("required"_str, [] {
    });

    if (result.is_err()) {
        EXPECT_TRUE(result.unwrap_err_unchecked().is_CounterUnavailable());
        return;
    }

    auto measured = rstd::move(result).unwrap_unchecked();
    EXPECT_TRUE(measured.counter_availability().is_Available());
    ASSERT_EQ(measured.measurements().len(), usize(11));
    EXPECT_TRUE(measured.measurements()[usize()].counters.instructions.is_some());
}

TEST(BenchProbe, RegistryIsIdempotentAndOwnsLabels) {
    auto registry = rstd::bench::probe::ProbeRegistry::new_();
    auto first    = registry.register_probe("load.mesh"_str).unwrap();
    auto second   = registry.register_probe("load.mesh"_str).unwrap();
    auto schema   = rstd::move(registry).freeze();

    EXPECT_EQ(first, second);
    ASSERT_EQ(schema.deref()->len(), usize(1));
    ASSERT_TRUE(schema.deref()->label(first).is_some());
    EXPECT_TRUE(schema.deref()->label(first).unwrap() == "load.mesh"_str);
}

TEST(BenchProbe, GuardMoveNestedSpansAndFrameUseStructuredSamples) {
    auto registry = rstd::bench::probe::ProbeRegistry::new_();
    auto parent   = registry.register_probe("render"_str).unwrap();
    auto child    = registry.register_probe("render.visibility"_str).unwrap();
    auto session  = rstd::bench::probe::BasicProbeSession<FakeClock>(rstd::move(registry).freeze(),
                                                                     FakeClock {});
    auto recorder = session.recorder();

    ASSERT_TRUE(recorder.begin_frame(u64(7)).is_ok());
    {
        auto outer = recorder.span(parent);
        auto inner = recorder.span(child);
        auto moved = rstd::move(inner);
        ASSERT_TRUE(moved.finish().is_ok());
    }
    auto result = recorder.end_frame();

    ASSERT_TRUE(result.is_ok());
    auto batch = rstd::move(result).unwrap_unchecked();
    ASSERT_EQ(batch.samples().len(), usize(2));
    const auto& child_sample  = batch.samples()[usize()];
    const auto& parent_sample = batch.samples()[usize(1)];
    EXPECT_EQ(child_sample.parent_sequence, Some(parent_sample.sequence));
    EXPECT_EQ(child_sample.frame, Some(u64(7)));
    EXPECT_EQ(parent_sample.frame, Some(u64(7)));
    EXPECT_EQ(child_sample.elapsed, rstd::time::Duration::from_nanos(u64(100)));
    EXPECT_EQ(parent_sample.elapsed, rstd::time::Duration::from_nanos(u64(300)));
}

TEST(BenchProbe, DropNewestReportsLossAndRecycleReusesCapacity) {
    auto registry = rstd::bench::probe::ProbeRegistry::new_();
    auto probe    = registry.register_probe("load.asset"_str).unwrap();
    auto session  = rstd::bench::probe::BasicProbeSession<FakeClock>(rstd::move(registry).freeze(),
                                                                     FakeClock {});
    auto config   = rstd::bench::probe::RecorderConfig {};
    config.sample_capacity = usize(2);
    auto recorder          = session.recorder(rstd::move(config));

    for (usize index; index < usize(3); ++index) {
        auto span = recorder.span(probe);
    }
    auto result = recorder.drain();
    ASSERT_TRUE(result.is_ok());
    auto batch = rstd::move(result).unwrap_unchecked();
    EXPECT_EQ(batch.samples().len(), usize(2));
    EXPECT_EQ(batch.dropped_samples(), usize(1));
    ASSERT_TRUE(recorder.recycle(rstd::move(batch)).is_ok());
    EXPECT_EQ(recorder.sample_capacity(), usize(2));
}

TEST(BenchProbe, NonLifoFinishIsExplicitAndStickyAtBoundary) {
    auto registry = rstd::bench::probe::ProbeRegistry::new_();
    auto probe    = registry.register_probe("nested"_str).unwrap();
    auto session  = rstd::bench::probe::BasicProbeSession<FakeClock>(rstd::move(registry).freeze(),
                                                                     FakeClock {});
    auto recorder = session.recorder();
    auto outer    = recorder.span(probe);
    auto inner    = recorder.span(probe);

    auto finish = outer.finish();
    ASSERT_TRUE(finish.is_err());
    EXPECT_TRUE(finish.unwrap_err_unchecked().as_Diagnostic().reason.is_NonLifo());
    ASSERT_TRUE(inner.finish().is_ok());
    auto first_drain = recorder.drain();
    ASSERT_TRUE(first_drain.is_err());
    EXPECT_TRUE(first_drain.unwrap_err_unchecked().as_Diagnostic().reason.is_NonLifo());
    auto second_drain = recorder.drain();
    ASSERT_TRUE(second_drain.is_ok());
    EXPECT_EQ(second_drain.unwrap_unchecked().samples().len(), usize(2));
}

TEST(BenchProbe, InvalidProbeAndActiveCapacityReturnDiagnostics) {
    auto registry = rstd::bench::probe::ProbeRegistry::new_();
    auto probe    = registry.register_probe("valid"_str).unwrap();
    auto session  = rstd::bench::probe::BasicProbeSession<FakeClock>(rstd::move(registry).freeze(),
                                                                     FakeClock {});
    auto config   = rstd::bench::probe::RecorderConfig {};
    config.active_span_capacity = usize(1);
    auto recorder               = session.recorder(rstd::move(config));

    auto invalid = recorder.span(rstd::bench::probe::ProbeId::from_u32(u32(7)));
    EXPECT_FALSE(invalid.is_active());
    auto invalid_result = recorder.drain();
    ASSERT_TRUE(invalid_result.is_err());
    EXPECT_TRUE(invalid_result.unwrap_err_unchecked().as_Diagnostic().reason.is_InvalidProbe());

    auto outer    = recorder.span(probe);
    auto overflow = recorder.span(probe);
    EXPECT_TRUE(outer.is_active());
    EXPECT_FALSE(overflow.is_active());
    ASSERT_TRUE(outer.finish().is_ok());
    auto overflow_result = recorder.drain();
    ASSERT_TRUE(overflow_result.is_err());
    EXPECT_TRUE(
        overflow_result.unwrap_err_unchecked().as_Diagnostic().reason.is_ActiveSpanOverflow());
    auto batch = recorder.drain().unwrap();
    EXPECT_EQ(batch.samples().len(), usize(1));
}

TEST(BenchProbe, PendingSpanAndWrongThreadAreReportedAtBoundaries) {
    auto registry = rstd::bench::probe::ProbeRegistry::new_();
    auto probe    = registry.register_probe("thread-bound"_str).unwrap();
    auto session  = rstd::bench::probe::BasicProbeSession<FakeClock>(rstd::move(registry).freeze(),
                                                                     FakeClock {});
    auto recorder = session.recorder();

    auto active  = recorder.span(probe);
    auto pending = recorder.drain();
    ASSERT_TRUE(pending.is_err());
    EXPECT_TRUE(pending.unwrap_err_unchecked().as_Diagnostic().reason.is_ActiveSpansPending());
    ASSERT_TRUE(active.finish().is_ok());
    ASSERT_TRUE(recorder.drain().is_ok());

    auto worker = rstd::thread::spawn([&recorder, probe] {
                      auto span = recorder.span(probe);
                      return span.is_active();
                  }).unwrap();
    EXPECT_FALSE(rstd::move(worker).join().unwrap());
    auto wrong_thread = recorder.drain();
    ASSERT_TRUE(wrong_thread.is_err());
    EXPECT_TRUE(wrong_thread.unwrap_err_unchecked().as_Diagnostic().reason.is_WrongThread());
}

TEST(BenchProbe, SessionRecordersShareSchemaAndCollectorTimeline) {
    auto registry = rstd::bench::probe::ProbeRegistry::new_();
    auto probe    = registry.register_probe("shared"_str).unwrap();
    auto schema   = rstd::move(registry).freeze();
    auto session  = rstd::bench::probe::BasicProbeSession<FakeClock>(schema.clone(), FakeClock {});
    auto first    = session.recorder();
    auto second   = session.recorder();

    {
        auto span = first.span(probe);
    }
    {
        auto span = second.span(probe);
    }
    auto first_batch  = first.drain().unwrap();
    auto second_batch = second.drain().unwrap();
    EXPECT_NE(first_batch.recorder_id(), second_batch.recorder_id());
    EXPECT_LT(first_batch.samples()[usize()].started_ns,
              second_batch.samples()[usize()].started_ns);

    auto collector = rstd::bench::probe::ProbeCollector::new_(schema.clone());
    ASSERT_TRUE(collector.ingest(first_batch).is_ok());
    ASSERT_TRUE(collector.ingest(second_batch).is_ok());
    auto report = rstd::move(collector).finish();
    ASSERT_EQ(report.overall().len(), usize(1));
    EXPECT_EQ(report.overall()[usize()].count, usize(2));
}

TEST(BenchProbe, CollectorComputesNestedExclusiveFrameAndTextReport) {
    auto registry = rstd::bench::probe::ProbeRegistry::new_();
    auto parent   = registry.register_probe("frame"_str).unwrap();
    auto child    = registry.register_probe("frame.draw"_str).unwrap();
    auto schema   = rstd::move(registry).freeze();
    auto session  = rstd::bench::probe::BasicProbeSession<FakeClock>(schema.clone(), FakeClock {});
    auto recorder = session.recorder();

    recorder.begin_frame(u64(12)).unwrap();
    {
        auto outer = recorder.span(parent);
        {
            auto inner = recorder.span(child);
        }
    }
    auto batch     = recorder.end_frame().unwrap();
    auto collector = rstd::bench::probe::ProbeCollector::new_(schema.clone());
    ASSERT_TRUE(collector.ingest(batch).is_ok());
    collector.record_diagnostic(rstd::bench::probe::ProbeDiagnostic::NoActiveFrame());
    ASSERT_TRUE(recorder.recycle(rstd::move(batch)).is_ok());
    auto report = rstd::move(collector).finish();

    ASSERT_EQ(report.overall().len(), usize(2));
    ASSERT_EQ(report.by_thread().len(), usize(2));
    ASSERT_EQ(report.by_frame().len(), usize(2));
    ASSERT_EQ(report.diagnostics().len(), usize(1));
    const auto& parent_summary = report.overall()[usize(1)];
    EXPECT_EQ(parent_summary.probe, parent);
    EXPECT_EQ(parent_summary.inclusive_total, rstd::time::Duration::from_nanos(u64(300)));
    EXPECT_EQ(parent_summary.exclusive_total, rstd::time::Duration::from_nanos(u64(200)));

    auto output = rstd::io::Cursor<Vec<u8>>(Vec<u8>::make());
    ASSERT_TRUE(rstd::bench::probe::write_text(output, report).is_ok());
    EXPECT_GT(output.get_ref().len(), usize());
    auto text = rstd::str_::from_utf8(output.get_ref().as_slice()).unwrap();
    EXPECT_TRUE(text.find("frame=12"_str).is_some());
    EXPECT_TRUE(text.find("diagnostic=no_active_frame"_str).is_some());
}
#endif
