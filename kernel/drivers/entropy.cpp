#include "drivers/entropy.h"
#include "drivers/virtio_rng.h"
#include <kernel/log.h>

namespace entropy {
namespace {

static_assert(MAX_READ <= virtio_rng::MAX_READ);

class Source {
public:
    static int start() {
        Error error = start_platform();
        if (error == Error::NotSupported)
            return 0;
        if (error != Error::None) {
            klog::warn("random: hardware generator not started (%s)\n", error_str(error));
            return 0;
        }
        platform_ = true;
        klog::info("random: hardware generator ready\n");
        return 0;
    }

    static Result<size_t> read(void* output, size_t size) {
        ENSURE(size != 0 && size <= MAX_READ);
        if (platform_)
            return read_platform(output, size);
        return virtio_rng::read(output, size);
    }

private:
    inline static bool platform_{};
};

}  // namespace

int init() {
    return Source::start();
}

Result<size_t> read(void* output, size_t size) {
    return Source::read(output, size);
}

}  // namespace entropy
