export module rstd.bench:workload;
export import :benchmark;

using namespace rstd::prelude;

export namespace rstd::bench
{

template<typename Operation>
concept RepeatOperation = requires(Operation& operation) { operation(); };

template<typename Operation>
    requires RepeatOperation<Operation>
class Repeated {
    Operation operation_;
    RunConfig work_;

public:
    Repeated(Operation operation, RunConfig work)
        : operation_(rstd::move(operation)), work_(rstd::move(work)) {}

    template<typename Engine>
    auto run(Engine& engine, ref<str> name) -> Result<BenchmarkResult, BenchError> {
        return engine.run(name,
                          operation_,
                          RunConfig { work_.unit.clone(),
                                      work_.batch,
                                      work_.items_per_iteration,
                                      work_.bytes_per_iteration });
    }
};

template<typename Operation>
auto repeated(Operation operation, RunConfig work = {}) -> Repeated<Operation> {
    return { rstd::move(operation), rstd::move(work) };
}

template<typename Operation>
class Fallible {
    Operation operation_;
    RunConfig work_;

public:
    Fallible(Operation operation, RunConfig work)
        : operation_(rstd::move(operation)), work_(rstd::move(work)) {}
    template<typename Engine>
    auto run(Engine& engine, ref<str> name) -> Result<BenchmarkResult, BenchError> {
        return engine.run_fallible(name,
                                   operation_,
                                   RunConfig { work_.unit.clone(),
                                               work_.batch,
                                               work_.items_per_iteration,
                                               work_.bytes_per_iteration });
    }
};

template<typename Operation>
auto fallible(Operation operation, RunConfig work = {}) -> Fallible<Operation> {
    return { rstd::move(operation), rstd::move(work) };
}

template<typename Setup, typename Operation, bool Borrowed>
class Batched {
    Setup       setup_;
    Operation   operation_;
    BatchConfig batch_;
    RunConfig   work_;

public:
    Batched(Setup setup, Operation operation, BatchConfig batch, RunConfig work)
        : setup_(rstd::move(setup)),
          operation_(rstd::move(operation)),
          batch_(batch),
          work_(rstd::move(work)) {}

    template<typename Engine>
    auto run(Engine& engine, ref<str> name) -> Result<BenchmarkResult, BenchError> {
        auto work = RunConfig {
            work_.unit.clone(), work_.batch, work_.items_per_iteration, work_.bytes_per_iteration
        };
        if constexpr (Borrowed)
            return engine.run_batched_ref(name, setup_, operation_, batch_, rstd::move(work));
        else
            return engine.run_batched(name, setup_, operation_, batch_, rstd::move(work));
    }
};

template<typename Setup, typename Operation>
auto batched(Setup setup, Operation operation, BatchConfig batch = {}, RunConfig work = {})
    -> Batched<Setup, Operation, false> {
    return { rstd::move(setup), rstd::move(operation), batch, rstd::move(work) };
}

template<typename Setup, typename Operation>
auto batched_ref(Setup setup, Operation operation, BatchConfig batch = {}, RunConfig work = {})
    -> Batched<Setup, Operation, true> {
    return { rstd::move(setup), rstd::move(operation), batch, rstd::move(work) };
}

template<typename Workload, typename Validate, typename Finish>
class Session {
    Workload workload_;
    Validate validate_;
    Finish   finish_;
    bool     finished_ {};

public:
    Session(Workload workload, Validate validate, Finish finish)
        : workload_(rstd::move(workload)),
          validate_(rstd::move(validate)),
          finish_(rstd::move(finish)) {}

    Session(const Session&)                    = delete;
    auto operator=(const Session&) -> Session& = delete;
    Session(Session&& other)
        : workload_(rstd::move(other.workload_)),
          validate_(rstd::move(other.validate_)),
          finish_(rstd::move(other.finish_)),
          finished_(other.finished_) {
        other.finished_ = true;
    }
    ~Session() {
        if (! finished_) static_cast<void>(finish());
    }

    auto check() -> Result<empty, String> { return validate_(); }
    auto finish() -> Result<empty, String> {
        if (finished_) return Ok(empty {});
        finished_ = true;
        return finish_();
    }

    template<typename Engine>
    auto run(Engine& engine, ref<str> name) -> Result<BenchmarkResult, BenchError> {
        return workload_.run(engine, name);
    }
};

template<typename Workload, typename Validate, typename Finish>
auto session(Workload workload, Validate validate, Finish finish)
    -> Session<Workload, Validate, Finish> {
    return { rstd::move(workload), rstd::move(validate), rstd::move(finish) };
}

template<typename Make>
class Factory {
    Make make_;

public:
    explicit Factory(Make make): make_(rstd::move(make)) {}
    auto prepare() { return make_(); }
};

template<typename Make>
auto factory(Make make) -> Factory<Make> {
    return Factory<Make>(rstd::move(make));
}

} // namespace rstd::bench
