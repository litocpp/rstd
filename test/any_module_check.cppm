export module rstd.test.any_module_check;

import rstd;

export auto any_int_type_id_from_module() noexcept -> rstd::any::TypeId {
    return rstd::any::TypeId::of<int>();
}
