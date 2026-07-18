set(RSTD_IO_SOURCES
    io/mod.cppm
    io/error.cppm
    io/traits.cppm
    io/buffered.cppm
    io/cursor.cppm
    io/range.cppm
    io/util.cppm
    io/stdio.cppm
    PARENT_SCOPE)

list(APPEND RSTD_PRIVATE_SOURCES io/error.cpp)

set(RSTD_PRIVATE_SOURCES
    ${RSTD_PRIVATE_SOURCES}
    PARENT_SCOPE)
