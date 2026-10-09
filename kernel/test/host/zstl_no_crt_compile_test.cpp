// Compile the mini-cocos-facing header surface without a hosted STL or C headers.
// Declarations may name runtime symbols; the embedding kernel must define used ones.
#if __STDC_HOSTED__ != 0
#error "This fixture requires freestanding compilation"
#endif
#if defined(ZSTL_FREESTANDING) || defined(ZSTL_RUNTIME_DECLARATIONS_PROVIDED)
#error "The kernel consumes self-contained zstl declarations without a mode macro"
#endif

#include <sys/algorithm.hpp>
#include <sys/array.hpp>
#include <sys/cassert.hpp>
#include <sys/cmath.hpp>
#include <sys/cstddef.hpp>
#include <sys/cstdint.hpp>
#include <sys/cstdio.hpp>
#include <sys/cstdlib.hpp>
#include <sys/cstring.hpp>
#include <sys/functional.hpp>
#include <sys/iterator.hpp>
#include <sys/limits.hpp>
#include <sys/memory.hpp>
#include <sys/mutex.hpp>
#include <sys/new.hpp>
#include <sys/set.hpp>
#include <sys/string.hpp>
#include <sys/type_traits.hpp>
#include <sys/unordered_map.hpp>
#include <sys/utility.hpp>
#include <sys/vector.hpp>

#if __cplusplus >= 202002L
#include <sys/inplace_vector.hpp>
#endif

namespace zstl_no_crt_test {

sys::unique_ptr<int> make_owner() {
    return sys::unique_ptr<int>(new (sys::nothrow) int(42));
}

sys::string make_name(const char* name) {
    return sys::string(name);
}

int sum_values() {
    sys::vector<int> values{1, 2, 3};
    int sum{};
    for (int value : values) {
        sum += value;
    }
    return sum;
}

}  // namespace zstl_no_crt_test
