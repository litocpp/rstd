export module rstd.core:prelude;
import :ptr.ptr;
import :ptr.dyn;

import :marker;
import :clone;
import :cmp;
import :ops;
import :convert;

import :option;
import :result;

export namespace rstd::prelude
{

using rstd::mut_ptr;
using rstd::mut_ref;
using rstd::ptr;
using rstd::ref;
using rstd::slice;

using rstd::as_cast;

using rstd::as;
using rstd::dyn;
using rstd::Impl;
using rstd::Impled;
using rstd::TraitFuncs;
using rstd::WithTrait;

using rstd::Send;
using rstd::Sized;
using rstd::Sync;
using rstd::clone::Clone;

using rstd::Fn;
using rstd::FnMut;

using rstd::cmp::PartialEq;
using rstd::convert::AsMut;
using rstd::convert::AsRef;
using rstd::convert::From;
using rstd::convert::Into;

using rstd::None;
using rstd::Option;
using rstd::Some;

using rstd::Err;
using rstd::Ok;
using rstd::Result;

using rstd::str;
} // namespace rstd::prelude