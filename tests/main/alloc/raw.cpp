#include <rstd/test/gtest.hpp>
import rstd.alloc;

namespace
{
struct alignas(128) RawValue {
    int* drops;
    int  value;
    RawValue(int& drops, int value) noexcept: drops(&drops), value(value) {}
    RawValue(const RawValue&) = delete;
    ~RawValue() { ++*drops; }
    static void* operator new(rstd::size_t) = delete;
};

template<typename T>
concept RawCreatable = requires { ::alloc::raw_new<T>(); };
static_assert(! RawCreatable<int[]>);
static_assert(! RawCreatable<int[2]>);
static_assert(! RawCreatable<const int>);
static_assert(noexcept(::alloc::raw_new<RawValue>(rstd::mtp::declval<int&>(), 1)));
} // namespace

TEST(RawAlloc, ConstructsAlignedObjectWithoutTypedNew) {
    int   drops = 0;
    auto* value = ::alloc::raw_new<RawValue>(drops, 42);
    EXPECT_EQ(reinterpret_cast<rstd::uintptr_t>(value) % alignof(RawValue), 0u);
    EXPECT_EQ(value->value, 42);
    EXPECT_EQ(drops, 0);
    ::alloc::raw_delete(value);
    EXPECT_EQ(drops, 1);
}

TEST(RawAlloc, ValueInitializesAndAcceptsNullDelete) {
    auto* value = ::alloc::raw_new<int>();
    EXPECT_EQ(*value, 0);
    ::alloc::raw_delete(value);
    ::alloc::raw_delete(static_cast<int*>(nullptr));
    const auto* constant = ::alloc::raw_new<int>(17);
    EXPECT_EQ(*constant, 17);
    ::alloc::raw_delete(constant);
}

#if defined(__cpp_exceptions)
TEST(RawAlloc, ConstructorFailureReleasesStorageWithoutDestruction) {
    struct Throws {
        int* drops;
        explicit Throws(int& drops): drops(&drops) { throw 42; }
        ~Throws() { ++*drops; }
    };
    int  drops  = 0;
    bool caught = false;
    try {
        (void)::alloc::raw_new<Throws>(drops);
    } catch (int value) {
        caught = value == 42;
    }
    EXPECT_TRUE(caught);
    EXPECT_EQ(drops, 0);
}

TEST(RawAlloc, DestructorFailureStillReleasesStorage) {
    struct Throws {
        ~Throws() noexcept(false) { throw 17; }
    };
    bool caught = false;
    try {
        ::alloc::raw_delete(::alloc::raw_new<Throws>());
    } catch (int value) {
        caught = value == 17;
    }
    EXPECT_TRUE(caught);
}
#endif
