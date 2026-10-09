#include "test/test_defs.h"
#include "lib/result.h"

namespace result_test {

namespace {

class NoDefault {
public:
    NoDefault() = delete;
    explicit NoDefault(int value) : value_(value) {}
    int value() const { return value_; }

private:
    int value_;
};

class MoveOnly {
public:
    MoveOnly() = delete;
    MoveOnly(int value, int& owners) : value_(value), owners_(&owners) { ++*owners_; }
    MoveOnly(const MoveOnly&) = delete;
    MoveOnly& operator=(const MoveOnly&) = delete;
    MoveOnly(MoveOnly&& other) : value_(other.value_), owners_(other.owners_) { other.owners_ = nullptr; }
    MoveOnly& operator=(MoveOnly&&) = delete;
    ~MoveOnly() {
        if (owners_) {
            --*owners_;
        }
    }
    int value() const { return value_; }

private:
    int value_;
    int* owners_;
};

class CopyValue {
public:
    CopyValue(int value, int& live) : value_(value), live_(&live) { ++*live_; }
    CopyValue(const CopyValue& other) : value_(other.value_), live_(other.live_) { ++*live_; }
    CopyValue& operator=(const CopyValue&) = delete;
    ~CopyValue() { --*live_; }
    int value() const { return value_; }

private:
    int value_;
    int* live_;
};

static_assert(__is_constructible(Result<int>, const Result<int>&));
static_assert(__is_assignable(Result<int>&, const Result<int>&));
static_assert(!__is_constructible(Result<MoveOnly>, const Result<MoveOnly>&));
static_assert(!__is_assignable(Result<MoveOnly>&, const Result<MoveOnly>&));
static_assert(__is_constructible(Result<MoveOnly>, Result<MoveOnly>&&));
static_assert(__is_assignable(Result<MoveOnly>&, Result<MoveOnly>&&));

Result<int> try_value(int& calls, bool fail) {
    ++calls;
    if (fail) {
        return Error::Timeout;
    }
    return 21;
}

Result<int> propagate_value(int& calls, bool fail) {
    int value = TRY(try_value(calls, fail));
    return value * 2;
}

Result<void> void_status(int& calls, bool fail) {
    ++calls;
    return fail ? Error::Io : Error::None;
}

Error propagate_void(int& calls, bool fail) {
    TRY(void_status(calls, fail));
    TRY_LOG(Error::None, "Unexpected Error propagation failure");
    return Error::None;
}

Result<void> propagate_status(int& calls, bool fail) {
    ++calls;
    TRY_LOG(fail ? Error::Busy : Error::None, "Expected Result test error");
    ++calls;
    return Error::None;
}

Result<MoveOnly> make_resource(int& owners) {
    return MoveOnly(42, owners);
}

Result<MoveOnly> propagate_resource(int& owners) {
    auto value = TRY(make_resource(owners));
    return value;
}

}  // namespace

void test() {
    int tests_passed = 0;
    int tests_failed = 0;

    {
        TEST_START("Result value and error states");
        Result<int> success = 0;
        Result<int> failure = Error::NoMem;
        TEST_ASSERT(success.ok() && success.value() == 0 && success.error() == Error::None,
                    "Zero is a successful value, not a failure sentinel");
        TEST_ASSERT(!failure.ok() && !failure.is_consumed() && failure.error() == Error::NoMem,
                    "Failure preserves the original error");
        const auto& borrowed = success;
        TEST_ASSERT(borrowed.value_or(99) == 0 && failure.value_or(99) == 99,
                    "Borrowed fallback preserves either state");
        TEST_ASSERT(success.release_value() == 0 && success.is_consumed(), "Taking the value consumes the result");
        TEST_ASSERT(failure.release_error() == Error::NoMem && failure.is_consumed(),
                    "Taking the error consumes the result");
        TEST_ASSERT(static_cast<int>(Error::None) == 0 && static_cast<int>(Error::Io) == -1 &&
                        static_cast<int>(Error::Fail) == -13,
                    "Existing error numeric codes remain unchanged");
        TEST_END();
    }

    {
        TEST_START("Result copy and assignment transitions");
        Result<NoDefault> failure = Error::Busy;
        TEST_ASSERT(!failure.ok() && failure.error() == Error::Busy,
                    "Failure does not require a default-constructible value");
        Result<int> original = 7;
        Result<int> copy = original;
        copy.value() = 9;
        TEST_ASSERT(original.value() == 7 && copy.value() == 9, "Copyable values remain independent");
        copy = Error::Io;
        TEST_ASSERT(!copy.ok() && copy.error() == Error::Io, "Value-to-error assignment changes the active state");
        copy = original;
        TEST_ASSERT(copy.ok() && copy.value() == 7, "Error-to-value copy assignment constructs the value");
        auto& same = copy;
        copy = same;
        copy = static_cast<Result<int>&&>(same);
        TEST_ASSERT(copy.ok() && copy.value() == 7, "Self-copy and self-move keep the value alive");
        auto moved = static_cast<Result<int>&&>(original);
        TEST_ASSERT(moved.ok() && moved.value() == 7 && original.is_consumed(), "Moving consumes the source");
        TEST_END();
    }

    {
        TEST_START("Result copy lifetime without value assignment");
        int live = 0;
        {
            Result<CopyValue> original = CopyValue(7, live);
            auto copy = original;
            TEST_ASSERT(live == 2 && copy.value().value() == 7, "Copy construction creates an independent live value");
            copy = Error::Io;
            TEST_ASSERT(live == 1 && copy.error() == Error::Io, "Assigning an error destroys the previous value");
            copy = original;
            TEST_ASSERT(live == 2 && copy.value().value() == 7, "Copy assignment reconstructs a non-assignable value");
            copy = original;
            TEST_ASSERT(live == 2, "Replacing a copied value releases the old object");
            auto failure = Result<CopyValue>(Error::NoMem);
            original = failure;
            TEST_ASSERT(live == 1 && original.error() == Error::NoMem, "Copying an error never constructs a value");
        }
        TEST_ASSERT(live == 0, "Copied values are all destroyed at the end of their lifetime");
        TEST_END();
    }

    {
        TEST_START("Result move-only resource lifetime");
        int owners = 0;
        {
            Result<MoveOnly> failure = Error::NoMem;
            TEST_ASSERT(owners == 0 && !failure.ok(), "An error does not construct a resource");
            auto source = make_resource(owners);
            auto target = make_resource(owners);
            TEST_ASSERT(owners == 2, "Each successful result owns one resource");
            target = static_cast<Result<MoveOnly>&&>(source);
            TEST_ASSERT(owners == 1 && source.is_consumed() && target.value().value() == 42,
                        "Move assignment releases the old target and transfers the source");
            auto owned = target.release_value();
            TEST_ASSERT(owners == 1 && target.is_consumed() && owned.value() == 42,
                        "Taking a resource transfers ownership without duplication");
            auto via_try = propagate_resource(owners);
            TEST_ASSERT(via_try.ok() && via_try.value().value() == 42 && owners == 2,
                        "TRY propagates a non-copyable, non-assignable resource");
        }
        TEST_ASSERT(owners == 0, "All resource owners are released exactly once");
        TEST_END();
    }

    {
        TEST_START("Result consuming fallback");
        int owners = 0;
        {
            Result<MoveOnly> failure = Error::NotFound;
            auto fallback = static_cast<Result<MoveOnly>&&>(failure).value_or(MoveOnly(8, owners));
            TEST_ASSERT(failure.is_consumed() && fallback.value() == 8 && owners == 1,
                        "A failed result can consume a move-only fallback");
            auto success = make_resource(owners);
            auto value = static_cast<Result<MoveOnly>&&>(success).value_or(MoveOnly(9, owners));
            TEST_ASSERT(success.is_consumed() && value.value() == 42 && owners == 2,
                        "Successful extraction destroys the unused fallback");
        }
        TEST_ASSERT(owners == 0, "Fallback resources do not leak");
        TEST_END();
    }

    {
        TEST_START("Result void and TRY propagation");
        Result<void> success;
        Result<void> failure = Error::Io;
        TEST_ASSERT(success.ok() && success.error() == Error::None, "Void defaults to success");
        success.release_value();
        TEST_ASSERT(success.is_consumed(), "Void success can be consumed");
        TEST_ASSERT(failure.release_error() == Error::Io && failure.is_consumed(), "Void error can be consumed");
        int calls = 0;
        auto value = propagate_value(calls, false);
        TEST_ASSERT(value.ok() && value.value() == 42 && calls == 1, "TRY evaluates a successful expression once");
        auto error = propagate_value(calls, true);
        TEST_ASSERT(!error.ok() && error.error() == Error::Timeout && calls == 2,
                    "TRY preserves the error and stops the operation");
        TEST_ASSERT(propagate_void(calls, false) == Error::None && calls == 3, "TRY supports successful Result<void>");
        TEST_ASSERT(propagate_void(calls, true) == Error::Io && calls == 4, "TRY supports failed Result<void>");
        auto status = propagate_status(calls, false);
        TEST_ASSERT(status.ok() && calls == 6, "TRY_LOG supports a successful Error in a void result function");
        auto logged_error = propagate_status(calls, true);
        TEST_ASSERT(logged_error.error() == Error::Busy && calls == 7,
                    "TRY_LOG preserves the Error and returns before subsequent operations");
        Result<void> copy = logged_error;
        auto moved = static_cast<Result<void>&&>(logged_error);
        TEST_ASSERT(copy.error() == Error::Busy && moved.error() == Error::Busy && logged_error.is_consumed(),
                    "Void copy preserves the source and void move consumes it");
        copy = Error::None;
        moved = copy;
        TEST_ASSERT(copy.ok() && moved.ok(), "Void assignment replaces errors with success");
        TEST_END();
    }

    TEST_SUMMARY("Result Library");
}

}  // namespace result_test
