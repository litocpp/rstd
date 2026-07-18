set(RSTD_NET_SOURCES
    net/mod.cppm
    net/socket_addr.cppm
    net/tcp.cppm
    PARENT_SCOPE)

list(APPEND RSTD_PRIVATE_SOURCES net/tcp.cpp)

set(RSTD_PRIVATE_SOURCES
    ${RSTD_PRIVATE_SOURCES}
    PARENT_SCOPE)
