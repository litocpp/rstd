#pragma once

#define TRAIT_HELPER(C)          \
    template<typename, typename> \
    friend struct rstd::Impl;