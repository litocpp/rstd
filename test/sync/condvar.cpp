#include <gtest/gtest.h>
import rstd;

using namespace rstd;

namespace
{

struct StartState {
    sync::Mutex<bool> ready;
    sync::Condvar     cvar;

    StartState(): ready(false), cvar() {}
};

} // namespace

TEST(Condvar, WaitWhileWakesAfterNotifyOne) {
    auto state  = sync::Arc<StartState>::make();
    auto worker = state.clone();

    auto handle = thread::spawn([worker = rstd::move(worker)] {
        auto guard = worker->ready.lock().unwrap();
        *guard     = true;
        worker->cvar.notify_one();
    }).unwrap();

    auto guard = state->ready.lock().unwrap();
    state->cvar.wait_while(guard, [](bool ready) {
        return !ready;
    });

    EXPECT_TRUE(*guard);
    rstd::move(handle).join().unwrap();
}

TEST(Condvar, WaitTimeoutReportsElapsedTimeout) {
    auto mutex = sync::Mutex<bool>(false);
    auto cvar  = sync::Condvar {};
    auto guard = mutex.lock().unwrap();

    auto result = cvar.wait_timeout(guard, time::Duration::from_millis(5));

    EXPECT_TRUE(result.timed_out());
    EXPECT_FALSE(*guard);
}

TEST(Condvar, WaitTimeoutWhileReturnsWhenPredicateClears) {
    auto state  = sync::Arc<StartState>::make();
    auto worker = state.clone();

    auto handle = thread::spawn([worker = rstd::move(worker)] {
        thread::sleep(time::Duration::from_millis(5));
        auto guard = worker->ready.lock().unwrap();
        *guard     = true;
        worker->cvar.notify_all();
    }).unwrap();

    auto guard  = state->ready.lock().unwrap();
    auto result = state->cvar.wait_timeout_while(
        guard,
        time::Duration::from_millis(500),
        [](bool ready) {
            return !ready;
        });

    EXPECT_FALSE(result.timed_out());
    EXPECT_TRUE(*guard);
    rstd::move(handle).join().unwrap();
}
