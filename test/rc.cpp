#include <gtest/gtest.h>
#include <string>

import rstd.rc;

using namespace rstd::rc;

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

TEST(RcTest, BasicConstruction) {
    Rc<int> empty;
    EXPECT_EQ(empty.get(), nullptr);

    auto    ptr = new int(42);
    Rc<int> rc(ptr);
    EXPECT_EQ(*rc, 42);
    EXPECT_EQ(rc.strong_count(), 1);
    EXPECT_EQ(rc.weak_count(), 0);
}

TEST(RcTest, MakeRc) {
    auto rc = make_rc<std::string>("test");
    EXPECT_EQ(*rc, "test");
    EXPECT_EQ(rc.strong_count(), 1);
}

TEST(RcTest, CopyAndMove) {
    auto rc1 = make_rc<int>(42);
    auto rc2 = rc1;
    EXPECT_EQ(rc1.strong_count(), 2);
    EXPECT_EQ(rc2.strong_count(), 2);

    auto rc3 = std::move(rc2);
    EXPECT_EQ(rc2.get(), nullptr);
    EXPECT_EQ(rc1.strong_count(), 2);
    EXPECT_EQ(rc3.strong_count(), 2);
}

TEST(RcTest, Destruction) {
    bool destroyed = false;
    {
        auto rc = make_rc<TestStruct>(42, &destroyed);
        EXPECT_FALSE(destroyed);
    }
    EXPECT_TRUE(destroyed);
}

TEST(RcTest, WeakReference) {
    auto rc   = make_rc<int>(42);
    auto weak = rc.downgrade();

    EXPECT_EQ(rc.strong_count(), 1);
    EXPECT_EQ(rc.weak_count(), 1);

    {
        auto upgraded = weak.upgrade();
        ASSERT_TRUE(upgraded.has_value());
        EXPECT_EQ(**upgraded, 42);
        EXPECT_EQ(rc.strong_count(), 2);
    }

    rc            = Rc<int>(); // Reset original
    auto upgraded = weak.upgrade();
    EXPECT_FALSE(upgraded.has_value());
}

TEST(RcTest, CustomDeleter) {
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

TEST(RcTest, ArraySupport) {
    auto rc = make_rc<int[]>(3, 42);
    EXPECT_EQ(rc.get()[0], 42);
    EXPECT_EQ(rc.get()[1], 42);
    EXPECT_EQ(rc.get()[2], 42);
}

TEST(RcTest, ArrayOfStructs) {
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

TEST(RcTest, Uniqueness) {
    auto rc1 = make_rc<int>(42);
    EXPECT_TRUE(rc1.is_unique());

    auto weak = rc1.downgrade();
    EXPECT_FALSE(rc1.is_unique());

    auto rc2 = rc1;
    EXPECT_FALSE(rc1.is_unique());
    EXPECT_FALSE(rc2.is_unique());
}

TEST(RcTest, SwapOperation) {
    auto rc1 = make_rc<int>(1);
    auto rc2 = make_rc<int>(2);

    swap(rc1, rc2);
    EXPECT_EQ(*rc1, 2);
    EXPECT_EQ(*rc2, 1);
}

TEST(RcTest, CustomAllocator) {
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

TEST(RcTest, Size) {
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

TEST(RcTest, Constness) {
    auto        val       = make_rc<int>(42);
    const auto& const_val = val;
    int         x         = *const_val;
}
