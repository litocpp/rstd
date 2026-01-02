export module rstd.sync.mpsc:mpmc;
export import :mpmc.counter;
export import :mpmc.array;
export import rstd.core;

namespace rstd::sync::mpsc::mpmc
{

template<typename T>
struct EnumSenderFlavor {
    struct Array {
        // counter::Sender<array::Channel<T>> val;
    };
    struct List {
        // counter::Sender<list::Channel<T>> val;
    };
    struct Zero {
        // counter::Sender<zero::Channel<T>> val;
    };
    using type = cppstd::variant<Array, List, Zero>;
};

template<typename T>
using SenderFlavor = EnumSenderFlavor<T>::type;

export template<typename T>
struct Sender {
    SenderFlavor<T> flavor;
};

}; // namespace rstd::sync::mpsc::mpmc