#include <gtest/gtest.h>
#include <sstream>
#include <string>
#include <any>

import rstd;

// Define a trait
template<typename T>
struct MyTrait {
    void reset() { M::template call<0>(this); }
    void display(std::ostream& os) const { M::template call<1>(this, os); }

private:
    using M = rstd::TraitMeta<MyTrait, T>;
    friend M;
    template<typename F>
    static consteval auto collect() {
        return rstd::TraitApi { &F::reset, &F::display };
    }
};

// Separate fields
struct PointFields {
    int x { 0 };
    int y { 0 };
};

struct Point;

// Implement trait
template<>
struct rstd::Impl<MyTrait, Point> {
    static void reset(rstd::TraitPtr self);

    static void display(const rstd::TraitPtr self, std::ostream& os) {
        const auto& p = self.as_ref<PointFields>();
        os << "Point(" << p.x << ", " << p.y << ")";
    }
};

// Define your class with trait
struct Point : public PointFields, public rstd::WithTrait<Point, MyTrait> {};

// Separte func impl if need to use Point or without separate fields
void rstd::Impl<MyTrait, Point>::reset(rstd::TraitPtr self) {
    auto& p = self.as_ref<Point>();
    p.x     = 0;
    p.y     = 0;
}

void example() {
    Point p { 10, 20 };
    p.display(std::cout);

    // Dynamic dispatch
    auto dyn = rstd::make_dyn<MyTrait>(p);
    dyn.display(std::cout);
}

TEST(TraitTest, ExampleTest) {
    example();
    EXPECT_TRUE(true);
}
