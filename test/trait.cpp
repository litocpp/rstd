#include <gtest/gtest.h>
#include <sstream>
#include <string>
#include <any>

import rstd;

// Example traits for testing
struct CloneTrait {
    template<typename T, typename = void>
    struct Api {
        using Trait = CloneTrait;
        T clone() const { return rstd::trait_call<0>(this); }
    };

    template<typename T>
    using Funcs = rstd::TraitFuncs<&T::clone>;
};

struct DisplayTrait {
    template<typename T, typename = void>

    struct Api {
        using Trait = DisplayTrait;
        void display(std::ostream& os) const { return rstd::trait_call<0>(this, os); }
    };

    template<typename T>
    using Funcs = rstd::TraitFuncs<&T::display>;
};

struct AddableTrait {
    template<typename T, typename = void>
    struct Api {
        using Trait = AddableTrait;
        T add(const T& other) const { return rstd::trait_call<0>(this, other); }
    };

    template<typename T>
    using Funcs = rstd::TraitFuncs<&T::add>;
};

struct MultiplyableTrait {
    template<typename T, typename = void>
    struct Api {
        using Trait = MultiplyableTrait;
        T multiply(int factor) const { return rstd::trait_call<0>(this, factor); }
    };

    template<typename T>
    using Funcs = rstd::TraitFuncs<&T::multiply>;
};

struct StringConverterTrait {
    template<typename T, typename = void>
    struct Api {
        using Trait = StringConverterTrait;
        std::string to_string() const { return rstd::trait_call<0>(this); }
        std::string to_hex() const { return rstd::trait_call<1>(this); }
        std::string to_binary() const { return rstd::trait_call<2>(this); }
    };

    template<typename T>
    using Funcs = rstd::TraitFuncs<&T::to_string, &T::to_hex, &T::to_binary>;
};

// Base structure for fields
struct TestClassFields {
    int value;
    explicit TestClassFields(int v = 0): value(v) {}
};

struct TestClass;

template<>
struct rstd::Impl<CloneTrait, TestClass> : rstd::LinkClassMethod<CloneTrait, TestClass> {};

template<>
struct rstd::Impl<DisplayTrait, TestClass> : rstd::LinkClassMethod<DisplayTrait, TestClass> {};

template<>
struct rstd::Impl<AddableTrait, TestClass> : rstd::LinkClassMethod<AddableTrait, TestClass> {};

template<>
struct rstd::Impl<MultiplyableTrait, TestClass>
    : rstd::LinkClassMethod<MultiplyableTrait, TestClass> {};

template<>
struct rstd::Impl<StringConverterTrait, TestClass>
    : rstd::LinkClassMethod<StringConverterTrait, TestClass> {};

struct TestClass : public TestClassFields {
    using TestClassFields::TestClassFields;

    TestClass clone() const { return TestClass(value); }

    void display(std::ostream& os) const { os << "TestClass(" << value << ")"; }

    TestClass add(const TestClass& other) const { return TestClass(value + other.value); }

    TestClass multiply(int factor) const { return TestClass(value * factor); }

    std::string to_string() const { return std::to_string(value); }

    std::string to_hex() const {
        std::stringstream ss;
        ss << "0x" << std::hex << value;
        return ss.str();
    }

    std::string to_binary() const {
        if (value == 0) return "0b0";
        std::string result = "0b";
        for (int i = sizeof(int) * 8 - 1; i >= 0; --i) {
            if (result.length() > 2 || (value & (1 << i))) {
                result += (value & (1 << i)) ? '1' : '0';
            }
        }
        return result;
    }
};

TEST(Trait, DisplayTrait) {
    TestClass          obj(42);
    std::ostringstream oss;
    obj.display(oss);
    EXPECT_EQ(oss.str(), "TestClass(42)");
}

TEST(Trait, DynamicDispatchTest) {
    TestClass          obj(42);
    auto               dyn = rstd::dyn<DisplayTrait>::from_ref(obj);
    std::ostringstream oss;
    dyn->display(oss);
    EXPECT_EQ(oss.str(), "TestClass(42)");
}

TEST(Trait, ComplexTrait) {
    TestClass a(10);
    TestClass b(5);

    auto sum = a.add(b);
    EXPECT_EQ(sum.value, 15);

    auto product = sum.multiply(2);
    EXPECT_EQ(product.value, 30);
    std::ostringstream oss;
    product.display(oss);
    EXPECT_EQ(oss.str(), "TestClass(30)");
}

// Add new test at the end
TEST(Trait, StringConverterTrait) {
    TestClass obj(42);

    // Test direct trait usage
    EXPECT_EQ(obj.to_string(), "42");
    EXPECT_EQ(obj.to_hex(), "0x2a");
    EXPECT_EQ(obj.to_binary(), "0b101010");

    // Test dynamic dispatch
    auto converter = rstd::dyn<StringConverterTrait>::from_ref(obj);
    EXPECT_EQ(converter->to_string(), "42");
    EXPECT_EQ(converter->to_hex(), "0x2a");
    EXPECT_EQ(converter->to_binary(), "0b101010");
}

TEST(Trait, StringConverterDynTest) {
    TestClass obj(255);

    // Test dynamic dispatch through pointer
    auto dyn1 = rstd::dyn<StringConverterTrait>::from_ptr(&obj);
    EXPECT_EQ(dyn1->to_string(), "255");
    EXPECT_EQ(dyn1->to_hex(), "0xff");
    EXPECT_EQ(dyn1->to_binary(), "0b11111111");

    // Test dynamic dispatch through reference
    auto dyn2 = rstd::dyn<StringConverterTrait>::from_ref(obj);
    EXPECT_EQ(dyn2->to_string(), "255");
    EXPECT_EQ(dyn2->to_hex(), "0xff");

    // Test moving Dyn object
    auto dyn3 = std::move(dyn1);
    EXPECT_EQ(dyn3->to_binary(), "0b11111111");

    // Test const reference
    const TestClass& const_ref = obj;
    auto             dyn4      = rstd::dyn<StringConverterTrait>::from_ref(const_ref);
    EXPECT_EQ(dyn4->to_string(), "255");
}

TEST(Trait, ConstDynTest) {
    const TestClass obj(42);

    // Test const dynamic dispatch
    auto               dyn = rstd::dyn<DisplayTrait>::from_ref(obj);
    std::ostringstream oss;
    dyn->display(oss);
    EXPECT_EQ(oss.str(), "TestClass(42)");

    // Test const dynamic dispatch with multiple traits
    auto str_dyn = rstd::dyn<StringConverterTrait>::from_ref(obj);
    EXPECT_EQ(str_dyn->to_string(), "42");
    EXPECT_EQ(str_dyn->to_hex(), "0x2a");
    EXPECT_EQ(str_dyn->to_binary(), "0b101010");

    // Test const pointer with dynamic dispatch
    const TestClass* ptr     = &obj;
    auto             ptr_dyn = rstd::dyn<StringConverterTrait>::from_ptr(ptr);
    EXPECT_EQ(ptr_dyn->to_string(), "42");

    // Verify that the type system prevents modification
    static_assert(std::is_same_v<decltype(dyn), rstd::ref<rstd::dyn<DisplayTrait>>>,
                  "Should be const Dyn type");
    static_assert(std::is_same_v<decltype(str_dyn), rstd::ref<rstd::dyn<StringConverterTrait>>>,
                  "Should be const Dyn type");
}

struct InClass {
    int  a;
    auto clone() const -> InClass { return { a }; }
};

struct InClassDef : rstd::DefaultInClass<InClassDef, rstd::clone::Clone> {
    int  a;
    auto clone() const -> InClassDef {
        InClassDef o {};
        o.a = a;
        return o;
    }
};

template<>
struct rstd::Impl<rstd::clone::Clone, InClass>
    : rstd::LinkClassRequiredWithDefault<rstd::clone::Clone, InClass> {};
template<>
struct rstd::Impl<rstd::clone::Clone, InClassDef>
    : rstd::LinkClassMethod<rstd::clone::Clone, InClassDef> {};

TEST(Trait, InClass) {
    {
        InClass m { 100 };
        auto    mm = rstd::as<rstd::clone::Clone>(m);
        static_assert(! rstd::mtp::is_const<decltype(mm)>);
        auto n = mm.clone();
        (void)n;
        EXPECT_EQ(m.a, 100);
        InClass source { 7 };
        mm.clone_from(source);
        EXPECT_EQ(m.a, 7);
    }
    {
        InClassDef m, n;
        m.a = 100;
        n.a = 1000;
        rstd::as<rstd::clone::Clone>(m).clone_from(n);
        EXPECT_EQ(m.a, 1000);
        m.a = 888;
        n.clone_from(m);
        EXPECT_EQ(n.a, m.a);
    }
}

struct EqOnly : rstd::DefaultInClass<EqOnly, rstd::cmp::PartialEq<EqOnly>> {
    int a;
    explicit EqOnly(int v): a(v) {}
    auto eq(const EqOnly& other) noexcept -> bool { return a == other.a; }
};

template<>
struct rstd::Impl<rstd::cmp::PartialEq<EqOnly>, EqOnly>
    : rstd::LinkClassRequiredWithDefault<rstd::cmp::PartialEq<EqOnly>, EqOnly> {};

struct RequiredIter : rstd::DefaultInClass<RequiredIter, rstd::iter::Iterator> {
    using Item = int;

    int left;

    explicit RequiredIter(int v): left(v) {}

    auto next() -> rstd::Option<int> {
        if (left == 0) return rstd::None();
        return rstd::Some(left--);
    }
};

template<>
struct rstd::Impl<rstd::iter::Iterator, RequiredIter>
    : rstd::LinkClassRequiredWithDefault<rstd::iter::Iterator, RequiredIter> {};

TEST(Trait, RequiredWithDefault) {
    EqOnly a { 1 };
    EqOnly b { 2 };
    EXPECT_TRUE(a.eq(a));
    EXPECT_TRUE(a.ne(b));
    EXPECT_TRUE(rstd::as<rstd::cmp::PartialEq<EqOnly>>(a).eq(a));
    EXPECT_TRUE(rstd::as<rstd::cmp::PartialEq<EqOnly>>(a).ne(b));

    RequiredIter direct { 3 };
    EXPECT_EQ(direct.count(), 3);

    RequiredIter via_trait { 2 };
    EXPECT_EQ(rstd::as<rstd::iter::Iterator>(via_trait).count(), 2);
}

using rstd::boxed::Box;
TEST(Trait, Dyn) {
    using namespace rstd;
    auto b = Box<dyn<DisplayTrait>>::from_raw(dyn<DisplayTrait>::from_ptr(new TestClass { 42 }));
    std::ostringstream oss;
    b->display(oss);
    EXPECT_EQ(oss.str(), "TestClass(42)");
}
