import rstd;

using namespace rstd::literals;

constexpr auto INVALID = "\xff"_str;

int main() { return static_cast<int>(INVALID.size().to_primitive()); }
