# rstd::rc

A Rust-like reference-counting smart pointer implementation for C++20.

## Features

- C++ module only
- Provides `Rc<T>` for single-threaded reference counting
- Support for weak references via `Weak<T>`
- Multiple storage policies:
  - `Embed`: Embeds the object directly in the control block
  - `Separate`: Allocates object separately from control block
  - `SeparateWithDeleter`: Supports custom deleters
- Custom allocator support
- Array support

## Requirements

- C++20 capable compiler
- CMake 3.28 or newer

## Installation

```bash
cmake -B build -S .
cmake --build build
```

## Usage

### Basic Usage

```cpp
import rstd.rc;

// Create a new reference-counted integer
auto num = rstd::rc::make_rc<int>(42);

// Create another reference
auto num2 = num;

// Access the value
std::cout << *num << std::endl; // prints 42

// Create a weak reference
auto weak = num.downgrade();

// Check reference counts
std::cout << num.strong_count() << std::endl; // 2
std::cout << num.weak_count() << std::endl;   // 1
```

### Storage Policies

```cpp
// Embedded storage
auto embedded = rstd::rc::make_rc<int, rstd::rc::StoragePolicy::Embed>(42);

// Separate storage (default)
auto separate = rstd::rc::make_rc<int>(42);

// Custom deleter
auto custom = rstd::rc::Rc<int>(new int(42), [](int* p) { delete p; });
```

### Array Support

```cpp
// Create a reference-counted array
auto arr = rstd::rc::make_rc<int[]>(5, 0); // Array of 5 integers initialized to 0
```

### Custom Allocator

```cpp
#include <memory>

std::allocator<int> alloc;
auto allocated = rstd::rc::allocate_make_rc<int>(alloc, 42);
```