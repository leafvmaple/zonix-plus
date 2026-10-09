# Error and Result contract

`kernel/lib/result.h` provides the common internal error vocabulary and result
container. It works with the kernel's freestanding C++20 configuration without
exceptions, RTTI, a standard library dependency or container-side allocation.

## Choosing a return type

- Use `Error` when an operation returns only a status. `Error::None` means success.
- Use `Result<T>` when success returns a value and failure returns an `Error`.
  Zero, null and other sentinel-looking values are still successful values;
  each operation must document whether they have a special domain meaning.
- `Result<void>` remains supported as a status wrapper and works with `TRY`.
  Prefer `Error` for new status-only kernel interfaces.
- Keep externally specified syscall, firmware and assembly return conventions.
  Translate internal errors at the boundary; do not change an ABI as part of an
  internal return-type migration.

The existing `Error` numeric values and `error_str()` strings are unchanged.
This foundation change does not migrate subsystem interfaces or invent new
error categories. Migrate a complete caller/callee chain in a separate change.

## States and ownership

| State | `ok()` | `is_consumed()` | Permitted access |
| --- | --- | --- | --- |
| Value | true | false | `value()`, `value_or()`, `error()` returns None, release value |
| Error | false | false | `error()`, `value_or()`, release error |
| Consumed | false | true | State queries, destruction, assignment |

`Result<T>` constructs `T` only on success. Failure therefore does not require
a default constructor or create a resource. Constructing `Result<T>` from
`Error::None` is an assertion failure: a successful value must actually exist.
`Result<void>` has no value to construct, so both its default constructor and
`Error::None` represent success.

`value()` borrows a reference. It requires a successful lvalue result; access
on a temporary is deleted to prevent dangling references. The reference stays
valid only while that same value remains alive in the result.

`release_value()` returns the value by move construction, destroys the stored
moved-from value and consumes the result. `release_error()` requires an error,
returns it and consumes the result. Invalid access, including a second release
or accessing a consumed result, invokes the kernel assertion handler. These
assertions diagnose programming errors, not recoverable operation failures.

A result is copyable only if `T` is copy-constructible. Copying preserves the
source; moving consumes it. Assignment destroys an old value before
constructing its replacement, so `T` need not be assignable. Self-copy and
self-move preserve the state. Destruction destroys a live value exactly once.
Values must be unqualified object types that can be constructed from an rvalue;
use pointers for borrowed objects and `Error` directly rather than `Result<Error>`.

`value_or(fallback)` on an lvalue or const result returns a copy and leaves the
result unchanged. On a mutable rvalue it returns the stored value or fallback
by move and consumes the result. The fallback argument is evaluated eagerly
in both cases.

## Propagation

`TRY(expr)` accepts `Error`, `Result<void>` and `Result<T>`. It evaluates `expr`
once, returns the original error from the enclosing function on failure, and
extracts the value on success. Status-only success has a void result.
`TRY_LOG` follows the same rules and logs on failure; its caller must provide
the logging declaration. Both macros use the existing GNU statement-expression
extension supported by the kernel toolchain.

```cpp
Result<Buffer> load_buffer();
Error write_buffer(const Buffer& buffer);

Error copy_buffer() {
    auto buffer = TRY(load_buffer());
    TRY(write_buffer(buffer));
    return Error::None;
}
```

Propagation owns a temporary result. Passing a copyable lvalue copies it;
explicitly moving an lvalue transfers ownership and consumes it. A move-only
lvalue must be moved explicitly. Do not reuse a consumed result until assigning
a new value or error to it. Ensure any acquired resources use destructors so
early propagation releases them.

## Verification

`kernel/test/unit/lib/result_test.cpp` exercises state transitions, copying,
moving, resource lifetimes, fallback extraction and propagation in the kernel.
`kernel/test/host/result_contract_test.cpp` supplies the host test entry point
and assertion handler. `scripts/tests/test_result.py` runs that suite, verifies
invalid accesses terminate, and checks compilation rejects borrowing a
temporary or ignoring a result. The normal harness test discovery includes it.
Test entry points and assertion substitutes remain under `kernel/test`.
