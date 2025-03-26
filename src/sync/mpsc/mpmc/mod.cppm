module;
#include <variant>

export module mpmc;
export import :counter;
export import :array;

namespace mpmc
{

template<typename T>
struct EnumSenderFlavor {
    struct Array {
        counter::Sender<array::Channel<T>> val;
    };
    struct List {
        //counter::Sender<list::Channel<T>> val;
    };
    struct Zero {
        //counter::Sender<zero::Channel<T>> val;
    };
    using type = std::variant<Array, List, Zero>;
};

template<typename T>
using SenderFlavor = EnumSenderFlavor<T>::type;

export template<typename T>
struct Sender {
    SenderFlavor<T> flavor;
};

}; // namespace mpsc