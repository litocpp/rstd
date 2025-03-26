export module mpmc_thread;

namespace mpmc::utils
{

class Thread {
    unsigned char handle[8];

public:
    Thread(const Thread&)            = delete;
    Thread& operator=(const Thread&) = delete;
    Thread(Thread&&);
    Thread& operator=(const Thread&&);

    static auto current() -> Thread;
};

} // namespace mpmc::utils

module :private;
