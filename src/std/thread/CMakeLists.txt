set(RSTD_THREAD_SOURCES
    thread/mod.cppm
    thread/id.cppm
    thread/thread.cppm
    thread/main_thread.cppm
    thread/builder.cppm
    thread/functions.cppm
    thread/join_handle.cppm
    thread/lifecycle.cppm
    thread/scoped.cppm
    thread/forward.cppm
    thread/current.cppm
    PARENT_SCOPE)

list(APPEND RSTD_PRIVATE_SOURCES thread/thread.cpp)

set(RSTD_PRIVATE_SOURCES
    ${RSTD_PRIVATE_SOURCES}
    PARENT_SCOPE)
