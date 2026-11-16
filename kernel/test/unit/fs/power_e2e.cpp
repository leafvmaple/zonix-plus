#include "fs/vfs.h"
#include "lib/kernel_buffer.h"
#include "lib/stdio.h"
#include <kernel/power.h>

namespace {

constexpr size_t PAYLOAD_SIZE = 65536 + 37;
constexpr char TAIL[] = "after-sync";
constexpr const char* DATA_PATH = "/P02DIR/STATE.BIN";

Error write_initial_file() {
    auto file = TRY(vfs::open(DATA_PATH, O_CREAT | O_TRUNC | O_RDWR));
    auto bytes = TRY(KernelBuffer::alloc(PAYLOAD_SIZE));
    for (size_t i = 0; i < bytes.size(); ++i) {
        bytes.data()[i] = static_cast<uint8_t>(i * 29 + 7);
    }
    int count = TRY(vfs::write(file.get(), bytes.data(), bytes.size(), 0));
    ENSURE(count == static_cast<int>(bytes.size()), Error::Io);
    return Error::None;
}

Error append_after_sync() {
    auto append = TRY(vfs::open(DATA_PATH, O_WRONLY | O_APPEND));
    int count = TRY(vfs::write(append.get(), TAIL, sizeof(TAIL) - 1, 0));
    ENSURE(count == sizeof(TAIL) - 1, Error::Io);
    TRY(vfs::create("/P02DIR/EMPTY.TXT"));
    return Error::None;
}

Error prepare_data() {
    Error error = vfs::mkdir("/P02DIR");
    ENSURE(error == Error::None || error == Error::Exists, error);
    TRY(write_initial_file());
    TRY(vfs::sync());
    cprintf("P0_2_EXPLICIT_SYNC_OK\n");
    return append_after_sync();
}

Error verify_data() {
    auto file = TRY(vfs::open(DATA_PATH, O_RDONLY));
    vfs::Stat st{};
    TRY(file->stat(&st));
    ENSURE(st.type == vfs::NodeType::File && st.size == PAYLOAD_SIZE + sizeof(TAIL) - 1, Error::Io);
    auto bytes = TRY(KernelBuffer::alloc(st.size));
    int count = TRY(vfs::read(file.get(), bytes.data(), bytes.size(), 0));
    ENSURE(count == static_cast<int>(bytes.size()), Error::Io);
    for (size_t i = 0; i < PAYLOAD_SIZE; ++i) {
        ENSURE(bytes.data()[i] == static_cast<uint8_t>(i * 29 + 7), Error::Io);
    }
    ENSURE(memcmp(bytes.data() + PAYLOAD_SIZE, TAIL, sizeof(TAIL) - 1) == 0, Error::Io);
    TRY(vfs::stat("/P02DIR/EMPTY.TXT", &st));
    ENSURE(st.type == vfs::NodeType::File && st.size == 0, Error::Io);
    cprintf("P0_2_PERSISTENCE_OK\n");
    return Error::None;
}

}  // namespace

namespace storage_sync_test {

// Only TEST=1 kernels inspect this marker, installed on temporary images by the runner.
bool run_power_e2e() {
    auto opened = vfs::open("/P02.RUN", O_RDONLY);
    if (!opened.ok() && opened.error() == Error::NotFound)
        return false;
    if (!opened.ok()) {
        cprintf("[FAIL] P0-2 E2E mode open: %s\n", error_str(opened.error()));
        return true;
    }
    auto file = opened.release_value();
    char mode[24]{};
    auto count = vfs::read(file.get(), mode, sizeof(mode) - 1, 0);
    file.reset();
    if (!count.ok()) {
        cprintf("[FAIL] P0-2 E2E mode read: %s\n", error_str(count.error()));
        return true;
    }
    const bool unsupported = strcmp(mode, "unsupported") == 0;
    Error error = strcmp(mode, "write") == 0 || unsupported ? prepare_data()
                  : strcmp(mode, "verify") == 0             ? verify_data()
                                                            : Error::Invalid;
    if (error != Error::None) {
        cprintf("[FAIL] P0-2 E2E data: %s\n", error_str(error));
        return true;
    }
    cprintf("P0_2_POWER_OFF_REQUEST\n");
    error = power::shutdown();
    if (unsupported && error == Error::NotSupported && !vfs::is_frozen()) {
        auto recovery = vfs::open("/P02DIR/RECOVER.TXT", O_CREAT | O_TRUNC | O_RDWR);
        if (recovery.ok()) {
            auto owner = recovery.release_value();
            auto written = vfs::write(owner.get(), "recovered", 9, 0);
            if (written.ok() && written.value() == 9 && vfs::sync() == Error::None) {
                cprintf("P0_2_SHUTDOWN_RECOVERED\n");
                return true;
            }
        }
    }
    cprintf("[FAIL] P0-2 E2E shutdown unexpectedly returned: %s\n", error_str(error));
    return true;
}

}  // namespace storage_sync_test
