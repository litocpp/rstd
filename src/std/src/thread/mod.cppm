//! The threading module, providing native OS threads, builders, and join handles.
export module rstd:thread;
export import :thread.id;
export import :thread.thread;
export import :thread.functions;
export import :thread.thread_pool;
export import :thread.blocking_task_group;
export import :thread.blocking_task_set;
export import :thread.builder;
export import :thread.join_handle;
export import :thread.current;
