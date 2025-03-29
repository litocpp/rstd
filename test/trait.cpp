#include <gtest/gtest.h>
#include <sstream>
#include <string>
#include <any>

import rstd;

// Example traits for testing
struct CloneTrait {
    template<typename T>
    struct Api {
        using Trait = CloneTrait;
        T clone() const { return rstd::trait_call<0>(this); }
    };

    template<typename T>
    using TCollect = rstd::TraitCollect<&T::clone>;
};

struct DisplayTrait {
    template<typename T>
    struct Api {
        using Trait = DisplayTrait;
        void display(std::ostream& os) const { return rstd::trait_call<0>(this, os); }
    };

    template<typename T>
    using TCollect = rstd::TraitCollect<&T::display>;
};

struct AddableTrait {
    template<typename T>
    struct Api {
        using Trait = AddableTrait;
        T add(const T& other) const { return rstd::trait_call<0>(this, other); }
    };

    template<typename T>
    using TCollect = rstd::TraitCollect<&T::add>;
};

struct MultiplyableTrait {
    template<typename T>
    struct Api {
        using Trait = MultiplyableTrait;
        T multiply(int factor) const { return rstd::trait_call<0>(this, factor); }
    };

    template<typename T>
    using TCollect = rstd::TraitCollect<&T::multiply>;
};

struct StringConverterTrait {
    template<typename T>
    struct Api {
        using Trait = StringConverterTrait;
        std::string to_string() const { return rstd::trait_call<0>(this); }
        std::string to_hex() const { return rstd::trait_call<1>(this); }
        std::string to_binary() const { return rstd::trait_call<2>(this); }
    };

    template<typename T>
    using TCollect = rstd::TraitCollect<&T::to_string, &T::to_hex, &T::to_binary>;
};

// Base structure for fields
struct TestClassFields {
    int value;
    explicit TestClassFields(int v = 0): value(v) {}
};

struct TestClass;

// Trait implementations declarations
template<>
struct rstd::Impl<CloneTrait, TestClass> {
    static TestClass clone(const rstd::TraitPtr self);
};

template<>
struct rstd::Impl<DisplayTrait, TestClass> {
    static void display(const rstd::TraitPtr self, std::ostream& os);
};

template<>
struct rstd::Impl<AddableTrait, TestClass> {
    static TestClass add(const rstd::TraitPtr self, const TestClass& other);
};

template<>
struct rstd::Impl<MultiplyableTrait, TestClass> {
    static TestClass multiply(const rstd::TraitPtr self, int factor);
};

// Add trait implementation declaration
template<>
struct rstd::Impl<StringConverterTrait, TestClass> {
    static std::string to_string(const rstd::TraitPtr self);
    static std::string to_hex(const rstd::TraitPtr self);
    static std::string to_binary(const rstd::TraitPtr self);
};

struct TestClass : public TestClassFields,
                   public rstd::WithTrait<TestClass, CloneTrait, DisplayTrait, AddableTrait,
                                          MultiplyableTrait, StringConverterTrait> {
    using TestClassFields::TestClassFields;
};

// Trait implementations definitions
TestClass rstd::Impl<CloneTrait, TestClass>::clone(const rstd::TraitPtr self) {
    return TestClass(self.as_ref<TestClass>().value);
}

void rstd::Impl<DisplayTrait, TestClass>::display(const rstd::TraitPtr self, std::ostream& os) {
    os << "TestClass(" << self.as_ref<TestClass>().value << ")";
}

TestClass rstd::Impl<AddableTrait, TestClass>::add(const rstd::TraitPtr self,
                                                   const TestClass&     other) {
    return TestClass(self.as_ref<TestClass>().value + other.value);
}

TestClass rstd::Impl<MultiplyableTrait, TestClass>::multiply(const rstd::TraitPtr self,
                                                             int                  factor) {
    return TestClass(self.as_ref<TestClass>().value * factor);
}

// Add implementations before the tests
std::string rstd::Impl<StringConverterTrait, TestClass>::to_string(const rstd::TraitPtr self) {
    return std::to_string(self.as_ref<TestClass>().value);
}

std::string rstd::Impl<StringConverterTrait, TestClass>::to_hex(const rstd::TraitPtr self) {
    std::stringstream ss;
    ss << "0x" << std::hex << self.as_ref<TestClass>().value;
    return ss.str();
}

std::string rstd::Impl<StringConverterTrait, TestClass>::to_binary(const rstd::TraitPtr self) {
    int value = self.as_ref<TestClass>().value;
    if (value == 0) return "0b0";
    std::string result = "0b";
    for (int i = sizeof(int) * 8 - 1; i >= 0; --i) {
        if (result.length() > 2 || (value & (1 << i))) result += (value & (1 << i)) ? '1' : '0';
    }
    return result;
}

// Add new tests
TEST(TraitTest, CloneTraitTest) {
    TestClass original(42);
    auto      cloned = std::any_cast<TestClass>(original.clone());
    EXPECT_EQ(cloned.value, 42);
}

TEST(TraitTest, DisplayTraitTest) {
    TestClass          obj(42);
    std::ostringstream oss;
    obj.display(oss);
    EXPECT_EQ(oss.str(), "TestClass(42)");
}

TEST(TraitTest, DynamicDispatchTest) {
    TestClass          obj(42);
    auto               dyn = rstd::Dyn<DisplayTrait>(obj);
    std::ostringstream oss;
    dyn.display(oss);
    EXPECT_EQ(oss.str(), "TestClass(42)");
}

TEST(TraitTest, ComplexTraitTest) {
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
TEST(TraitTest, StringConverterTraitTest) {
    TestClass obj(42);

    // Test direct trait usage
    EXPECT_EQ(obj.to_string(), "42");
    EXPECT_EQ(obj.to_hex(), "0x2a");
    EXPECT_EQ(obj.to_binary(), "0b101010");

    // Test dynamic dispatch
    auto converter = rstd::Dyn<StringConverterTrait>(obj);
    EXPECT_EQ(converter.to_string(), "42");
    EXPECT_EQ(converter.to_hex(), "0x2a");
    EXPECT_EQ(converter.to_binary(), "0b101010");
}

TEST(TraitTest, StringConverterDynTest) {
    TestClass obj(255);

    // Test dynamic dispatch through pointer
    auto dyn1 = rstd::Dyn<StringConverterTrait>(&obj);
    EXPECT_EQ(dyn1.to_string(), "255");
    EXPECT_EQ(dyn1.to_hex(), "0xff");
    EXPECT_EQ(dyn1.to_binary(), "0b11111111");

    // Test dynamic dispatch through reference
    auto dyn2 = rstd::Dyn<StringConverterTrait>(obj);
    EXPECT_EQ(dyn2.to_string(), "255");
    EXPECT_EQ(dyn2.to_hex(), "0xff");

    // Test moving Dyn object
    auto dyn3 = std::move(dyn1);
    EXPECT_EQ(dyn3.to_binary(), "0b11111111");

    // Test const reference
    const TestClass& const_ref = obj;
    auto             dyn4      = rstd::make_dyn<StringConverterTrait>(const_ref);
    EXPECT_EQ(dyn4.to_string(), "255");
}

TEST(TraitTest, ConstDynTest) {
    const TestClass obj(42);

    // Test const dynamic dispatch
    auto               dyn = rstd::make_dyn<DisplayTrait>(obj);
    std::ostringstream oss;
    dyn.display(oss);
    EXPECT_EQ(oss.str(), "TestClass(42)");

    // Test const dynamic dispatch with multiple traits
    auto str_dyn = rstd::make_dyn<StringConverterTrait>(obj);
    EXPECT_EQ(str_dyn.to_string(), "42");
    EXPECT_EQ(str_dyn.to_hex(), "0x2a");
    EXPECT_EQ(str_dyn.to_binary(), "0b101010");

    // Test const pointer with dynamic dispatch
    const TestClass* ptr     = &obj;
    auto             ptr_dyn = rstd::make_dyn<StringConverterTrait>(ptr);
    EXPECT_EQ(ptr_dyn.to_string(), "42");

    // Verify that the type system prevents modification
    static_assert(std::is_same_v<decltype(dyn), rstd::Dyn<const DisplayTrait>>,
                  "Should be const Dyn type");
    static_assert(std::is_same_v<decltype(str_dyn), rstd::Dyn<const StringConverterTrait>>,
                  "Should be const Dyn type");
}