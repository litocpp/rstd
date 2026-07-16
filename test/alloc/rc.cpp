#include <gtest/gtest.h>
#include <string>
#include <type_traits>

import rstd;

using namespace rstd;
using namespace rstd::rc;

static_assert(rstd::Impled<Rc<int>, rstd::ops::Deref>);
static_assert(! rstd::Impled<Rc<int>, rstd::ops::DerefMut>);

struct RcDynTrait {
    template<typename Self, typename = void>
    struct Api {
        using Trait = RcDynTrait;

        auto value() const noexcept -> int { return rstd::trait_call<0>(this); }
    };

    template<typename T>
    using Funcs = rstd::TraitFuncs<&T::value>;
};

struct alignas(64) RcDynPayload {
    int* drops;
    int  stored;

    RcDynPayload(int& drops, int value): drops(&drops), stored(value) {}
    RcDynPayload(const RcDynPayload&)            = delete;
    RcDynPayload& operator=(const RcDynPayload&) = delete;

    RcDynPayload(RcDynPayload&& other) noexcept: drops(other.drops), stored(other.stored) {
        other.drops = nullptr;
    }

    ~RcDynPayload() {
        if (drops != nullptr) ++*drops;
    }

    auto value() const noexcept -> int { return stored; }
};

struct RcDynEmptyPayload {
    auto value() const noexcept -> int { return 7; }
};

struct RcDynNoTrait {};

template<>
struct rstd::Impl<RcDynTrait, RcDynPayload> : rstd::LinkClassMethod<RcDynTrait, RcDynPayload> {};

template<>
struct rstd::Impl<RcDynTrait, RcDynEmptyPayload>
    : rstd::LinkClassMethod<RcDynTrait, RcDynEmptyPayload> {};

using RcDyn     = Rc<rstd::dyn<RcDynTrait>>;
using RcDynWeak = Weak<rstd::dyn<RcDynTrait>>;
using RcDynRaw  = RcRaw<rstd::dyn<RcDynTrait>>;
using RcDynPtr  = rstd::mut_ptr<rstd::dyn<RcDynTrait>>;

template<typename T, typename U>
concept CanMakeRc = requires(U&& value) { make_rc<T>(rstd::forward<U>(value)); };

static_assert(sizeof(Rc<int>) == sizeof(rstd::mut_ptr<int>));
static_assert(sizeof(Weak<int>) == sizeof(rstd::mut_ptr<int>));
static_assert(sizeof(Rc<int[]>) == sizeof(rstd::mut_ptr<int[]>));
static_assert(sizeof(Weak<int[]>) == sizeof(rstd::mut_ptr<int[]>));
static_assert(sizeof(RcDyn) == sizeof(RcDynPtr));
static_assert(sizeof(RcDynWeak) == sizeof(RcDynPtr));
static_assert(sizeof(RcDynRaw) == sizeof(RcDynPtr));
static_assert(CanMakeRc<RcDyn::Target, RcDynPayload>);
static_assert(! CanMakeRc<RcDyn::Target, RcDynNoTrait>);

struct TestStruct {
    int   value;
    bool* destroyed;

    explicit TestStruct(int v, bool* d): value(v), destroyed(d) {}
    ~TestStruct() {
        if (destroyed) *destroyed = true;
    }
};

template<typename T>
struct TestAllocator {
    bool* used_allocate   = nullptr;
    bool* used_deallocate = nullptr;

    using value_type = T;

    template<typename U>
    TestAllocator(TestAllocator<U> o)
        : used_allocate(o.used_allocate), used_deallocate(o.used_deallocate) {}

    TestAllocator(bool* alloc, bool* dealloc): used_allocate(alloc), used_deallocate(dealloc) {}

    T* allocate(size_t n) {
        if (used_allocate) *used_allocate = true;
        return static_cast<T*>(::operator new(n * sizeof(T)));
    }

    void deallocate(T* p, size_t) {
        if (used_deallocate) *used_deallocate = true;
        ::operator delete(p);
    }
};

struct ArrayTestStruct {
    int         value;
    std::string name;

    ArrayTestStruct(int v = 0, std::string n = ""): value(v), name(n) {}
    bool operator==(const ArrayTestStruct& other) const {
        return value == other.value && name == other.name;
    }
};

struct RcArrayDropProbe {
    int* drops;

    explicit RcArrayDropProbe(int& drops): drops(&drops) {}
    RcArrayDropProbe(const RcArrayDropProbe&) = default;

    ~RcArrayDropProbe() { ++*drops; }
};

TEST(Rc, BasicConstruction) {
    Rc<int> empty;
    EXPECT_EQ(empty.get(), nullptr);

    auto    ptr = new int(42);
    Rc<int> rc(ptr);
    EXPECT_EQ(*rc, 42);
    EXPECT_EQ(rc.strong_count(), 1);
    EXPECT_EQ(rc.weak_count(), 0);
}

TEST(Rc, MakeRc) {
    auto rc = make_rc<std::string>("test");
    EXPECT_EQ(*rc, "test");
    EXPECT_EQ(rc.strong_count(), 1);
}

TEST(Rc, DerefIsImmutable) {
    auto rc = make_rc<TestStruct>(7, nullptr);

    EXPECT_EQ(rc.deref().as_raw_ptr(), rc.get());
    EXPECT_EQ(rstd::as<rstd::ops::Deref>(rc).deref().as_raw_ptr(), rc.get());
    EXPECT_EQ(&*rc, rc.get());
    static_assert(std::is_same_v<decltype(*rc), const TestStruct&>);
    EXPECT_EQ(rc->value, 7);
}

TEST(Rc, CopyAndMove) {
    auto rc1 = make_rc<int>(42);
    auto rc2 = rc1;
    EXPECT_EQ(rc1.strong_count(), 2);
    EXPECT_EQ(rc2.strong_count(), 2);

    auto rc3 = std::move(rc2);
    EXPECT_EQ(rc2.get(), nullptr);
    EXPECT_EQ(rc1.strong_count(), 2);
    EXPECT_EQ(rc3.strong_count(), 2);
}

TEST(Rc, Destruction) {
    bool destroyed = false;
    {
        auto rc = make_rc<TestStruct>(42, &destroyed);
        EXPECT_FALSE(destroyed);
    }
    EXPECT_TRUE(destroyed);
}

TEST(Rc, WeakReference) {
    auto rc   = make_rc<int>(42);
    auto weak = rc.downgrade();

    EXPECT_EQ(rc.strong_count(), 1);
    EXPECT_EQ(rc.weak_count(), 1);

    {
        auto upgraded = weak.upgrade();
        ASSERT_TRUE(upgraded.is_some());
        EXPECT_EQ(**upgraded, 42);
        EXPECT_EQ(rc.strong_count(), 2);
    }

    rc            = Rc<int>(); // Reset original
    auto upgraded = weak.upgrade();
    EXPECT_FALSE(upgraded.is_some());
}

TEST(Rc, CustomDeleter) {
    bool custom_deleted = false;
    {
        auto deleter = [&custom_deleted](int* p) {
            custom_deleted = true;
            delete p;
        };
        Rc<int> rc(new int(42), deleter);
    }
    EXPECT_TRUE(custom_deleted);
}

TEST(Rc, ArraySupport) {
    auto rc = make_rc<int[]>(3, 42);
    EXPECT_EQ(rc.get()[0], 42);
    EXPECT_EQ(rc.get()[1], 42);
    EXPECT_EQ(rc.get()[2], 42);
}

TEST(Rc, ArrayOfStructs) {
    auto rc = make_rc<ArrayTestStruct[]>(3, ArrayTestStruct(42, "test"));

    EXPECT_EQ(rc.get()[0], ArrayTestStruct(42, "test"));
    EXPECT_EQ(rc.get()[1], ArrayTestStruct(42, "test"));
    EXPECT_EQ(rc.get()[2], ArrayTestStruct(42, "test"));

    // Modify elements
    rc.get()[1].value = 24;
    rc.get()[1].name  = "modified";

    EXPECT_EQ(rc.get()[0], ArrayTestStruct(42, "test"));
    EXPECT_EQ(rc.get()[1], ArrayTestStruct(24, "modified"));
    EXPECT_EQ(rc.get()[2], ArrayTestStruct(42, "test"));
}

TEST(Rc, Uniqueness) {
    auto rc1 = make_rc<int>(42);
    EXPECT_TRUE(rc1.is_unique());

    auto weak = rc1.downgrade();
    EXPECT_FALSE(rc1.is_unique());

    auto rc2 = rc1;
    EXPECT_FALSE(rc1.is_unique());
    EXPECT_FALSE(rc2.is_unique());
}

TEST(Rc, SwapOperation) {
    auto rc1 = make_rc<int>(1);
    auto rc2 = make_rc<int>(2);

    swap(rc1, rc2);
    EXPECT_EQ(*rc1, 2);
    EXPECT_EQ(*rc2, 1);
}

TEST(Rc, CustomAllocator) {
    bool allocated   = false;
    bool deallocated = false;
    {
        TestAllocator<int> alloc(&allocated, &deallocated);
        auto               rc = allocate_make_rc<int>(alloc, 42);

        EXPECT_TRUE(allocated);
        EXPECT_FALSE(deallocated);
        EXPECT_EQ(*rc, 42);
    }
    EXPECT_TRUE(deallocated);
}

TEST(Rc, CustomDeleterAndAllocator) {
    bool allocated   = false;
    bool deallocated = false;
    bool deleted     = false;
    {
        TestAllocator<int> allocator(&allocated, &deallocated);
        auto               deleter = [&deleted](int* pointer) {
            deleted = true;
            delete pointer;
        };
        Rc<int> rc(new int(31), deleter, allocator);
        EXPECT_TRUE(allocated);
        EXPECT_FALSE(deallocated);
        EXPECT_FALSE(deleted);
        EXPECT_EQ(*rc, 31);
    }
    EXPECT_TRUE(deleted);
    EXPECT_TRUE(deallocated);
}

TEST(Rc, CustomAllocatorSupportsArrayAndEmbedStorage) {
    bool allocated   = false;
    bool deallocated = false;
    int  drops       = 0;
    {
        TestAllocator<RcArrayDropProbe> allocator(&allocated, &deallocated);
        RcArrayDropProbe                initial { drops };
        {
            auto array = allocate_make_rc<RcArrayDropProbe[]>(allocator, 2, initial);
            EXPECT_EQ(array.as_ptr().len(), 2u);
        }
        EXPECT_EQ(drops, 2);
    }
    EXPECT_EQ(drops, 3);
    EXPECT_TRUE(allocated);
    EXPECT_TRUE(deallocated);

    allocated      = false;
    deallocated    = false;
    bool destroyed = false;
    {
        TestAllocator<TestStruct> allocator(&allocated, &deallocated);
        auto                      embedded =
            allocate_make_rc<TestStruct, StoragePolicy::Embed>(allocator, 47, &destroyed);
        EXPECT_EQ(embedded->value, 47);
    }
    EXPECT_TRUE(destroyed);
    EXPECT_TRUE(allocated);
    EXPECT_TRUE(deallocated);
}

TEST(Rc, Size) {
    // Test size for single object
    auto rc_single = make_rc<int>(42);
    EXPECT_EQ(rc_single.size(), 1);

    // Test size for array
    auto rc_array = make_rc<int[]>(5, 42);
    EXPECT_EQ(rc_array.size(), 5);

    // Test size for array of structs
    auto rc_struct_array = make_rc<ArrayTestStruct[]>(3, ArrayTestStruct(42, "test"));
    EXPECT_EQ(rc_struct_array.size(), 3);
}

TEST(Rc, Constness) {
    auto        val       = make_rc<int>(42);
    const auto& const_val = val;
    int         x         = *const_val;
    (void)x;
}

TEST(Rc, ConstConversionSharesAllocation) {
    auto          rc       = make_rc<int>(42);
    Rc<const int> const_rc = rc;

    EXPECT_EQ(*const_rc, 42);
    EXPECT_EQ(rc.strong_count(), 2u);
    EXPECT_TRUE(Rc<int>::ptr_eq(rc, rc));
    static_assert(std::is_same_v<decltype(const_rc.get()), const int*>);
}

TEST(Rc, EmbedStorageUsesPointerDrop) {
    bool destroyed = false;
    {
        auto rc = make_rc<TestStruct, StoragePolicy::Embed>(17, &destroyed);
        EXPECT_EQ(rc->value, 17);
        EXPECT_FALSE(destroyed);
    }
    EXPECT_TRUE(destroyed);
}

TEST(Rc, ArrayMetadataDrivesLayoutAndDrop) {
    int drops = 0;
    {
        RcArrayDropProbe initial { drops };
        {
            auto rc = make_rc<RcArrayDropProbe[]>(3, initial);
            EXPECT_EQ(rc.as_ptr().len(), 3u);
            EXPECT_EQ(rstd::alloc::Layout::for_value(rc.as_ptr().as_ptr()).size,
                      3 * sizeof(RcArrayDropProbe));
        }
        EXPECT_EQ(drops, 3);
    }
    EXPECT_EQ(drops, 4);
}

TEST(Rc, WeakCountRemainsCorrectAfterValueDrop) {
    Weak<int> weak;
    {
        auto rc = make_rc<int>(9);
        weak    = rc.downgrade();
        EXPECT_EQ(weak.weak_count(), 1u);
    }

    EXPECT_TRUE(weak.expired());
    EXPECT_EQ(weak.strong_count(), 0u);
    EXPECT_EQ(weak.weak_count(), 1u);
}

TEST(RcDyn, DispatchCopyWeakAndRawRoundtrip) {
    int  drops = 0;
    auto rc    = make_rc<rstd::dyn<RcDynTrait>>(RcDynPayload { drops, 41 });

    EXPECT_EQ(rc->value(), 41);
    EXPECT_EQ(reinterpret_cast<rstd::usize>(rc.as_ptr().as_raw_ptr()) % 64, 0u);

    auto copy      = rc;
    auto weak      = rc.downgrade();
    auto weak_copy = weak;
    EXPECT_EQ(weak.weak_count(), 2u);
    {
        auto upgraded = weak.upgrade();
        ASSERT_TRUE(upgraded.is_some());
        EXPECT_EQ((*upgraded)->value(), 41);
    }

    copy.reset();
    auto raw      = rc.into_raw();
    auto metadata = raw.as_ptr().metadata();
    auto restored = RcDyn::from_raw(rstd::move(raw));

    EXPECT_FALSE(rc);
    EXPECT_EQ(restored->value(), 41);
    EXPECT_EQ(restored.as_ptr().metadata(), metadata);
    EXPECT_EQ(weak.as_ptr().metadata(), metadata);
    EXPECT_EQ(weak_copy.as_ptr().metadata(), metadata);

    restored.reset();
    EXPECT_EQ(drops, 1);
    EXPECT_TRUE(weak.expired());
    EXPECT_TRUE(weak_copy.expired());
    EXPECT_EQ(weak.as_ptr().metadata(), metadata);
}

TEST(RcDyn, EmbedAndEmptyWeakPreserveMetadataState) {
    int drops = 0;
    {
        auto rc = make_rc<rstd::dyn<RcDynTrait>, StoragePolicy::Embed>(RcDynPayload { drops, 23 });
        EXPECT_EQ(rc->value(), 23);
    }
    EXPECT_EQ(drops, 1);

    RcDynWeak weak;
    EXPECT_TRUE(weak.expired());
    EXPECT_EQ(weak.as_ptr(), nullptr);
    EXPECT_EQ(weak.as_ptr().metadata(), nullptr);
}

TEST(RcDyn, CustomAllocatorPreservesDynDispatch) {
    bool allocated   = false;
    bool deallocated = false;
    {
        TestAllocator<RcDynEmptyPayload> allocator(&allocated, &deallocated);
        auto rc = allocate_make_rc<rstd::dyn<RcDynTrait>>(allocator, RcDynEmptyPayload {});
        EXPECT_TRUE(allocated);
        EXPECT_FALSE(deallocated);
        EXPECT_EQ(rc->value(), 7);
    }
    EXPECT_TRUE(deallocated);
}
