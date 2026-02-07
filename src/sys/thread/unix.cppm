export module rstd:sys.thread.unix;
export import :sys.libc.pthread;
export import :io;
export import :alloc;

#ifdef __linux__

using namespace rstd::sys::libc;
using rstd::alloc::boxed::Box;

namespace rstd::sys::thread::unix
{

export struct Thread {
    pthread_t id;

    // auto make(usize stack, Box<ThreadInit> init) -> io::Result<Thread> {
    //  let     data                                          = init;
    //  let mut attr : mem::MaybeUninit<libc::pthread_attr_t> = mem::MaybeUninit::uninit();
    //  assert_eq ! (libc::pthread_attr_init(attr.as_mut_ptr()), 0);
    //  let mut attr = DropGuard::new (
    //      &mut attr, | attr | { assert_eq ! (libc::pthread_attr_destroy(attr.as_mut_ptr()), 0) });
    //}
};

}; // namespace rstd::sys::thread::unix

#endif
