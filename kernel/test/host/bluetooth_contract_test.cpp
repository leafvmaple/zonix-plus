// Feeds the USB Bluetooth transport the interrupt IN packet sequences real
// adapters produce, including ones QEMU cannot (it has no Bluetooth adapter),
// and checks the events the HCI layer receives through what it logs.
#include "drivers/bluetooth/hci.h"
#include "drivers/intr.h"
#include "drivers/usb/usb_bluetooth.h"
#include "debug/assert.h"
#include "lib/memory.h"
#include "lib/stdarg.h"
#include "mm/pmm.h"
#include "sched/sched.h"
#include "trap/trap.h"
#include <kernel/log.h>

extern "C" int printf(const char*, ...);
extern "C" int vprintf(const char*, va_list);
extern "C" int vsnprintf(char*, size_t, const char*, va_list);
extern "C" [[noreturn]] void exit(int);

class LogFixture {
public:
    static void append(const char* format, va_list args) {
        const int written = vsnprintf(text_ + used_, sizeof(text_) - used_, format, args);
        if (written > 0)
            used_ = used_ + static_cast<size_t>(written) < sizeof(text_) ? used_ + written : sizeof(text_) - 1;
    }
    static bool contains(const char* needle) {
        const size_t length = strlen(needle);
        for (size_t at = 0; at + length <= used_; ++at)
            if (memcmp(text_ + at, needle, length) == 0)
                return true;
        return false;
    }
    static const char* text() { return text_; }

private:
    inline static char text_[8192]{};
    inline static size_t used_{};
};

intr::Guard::Guard() : flag_(0) {}
intr::Guard::~Guard() {}

void klog::write(klog::Level, const char* format, ...) {
    va_list args;
    va_start(args, format);
    LogFixture::append(format, args);
    va_end(args);
}

// Nothing in these paths waits: commands run in the Bluetooth thread, which
// the fixture does not start.
void WaitQueue::sleep() {
    assert(false);
}
void WaitQueue::wakeup_all() {}

// sched.h brings in address-space teardown, which nothing here reaches.
void pmm::free_user_pgdir(pde_t*) {
    assert(false);
}

uint64_t trap::uptime_ms() {
    return 0;
}

Result<int> sched::kernel_thread(KernelThreadEntry, void*) {
    return 2;
}

void panic_at(const char*, int line, const char* format, ...) {
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    exit(line);
}

namespace usb {

Location location(const Device&) {
    return {"1.4"};
}

}  // namespace usb

namespace {

class FakeHost final : public usb::Host {
public:
    Error control(usb::Device&, const usb::SetupPacket&, void*) override { return Error::None; }
    Error start_interrupt_in(usb::Device&, const usb::EndpointDescriptor&, usb::ReportHandler handler,
                             void* context) override {
        handler_ = handler;
        context_ = context;
        return Error::None;
    }
    Error set_hub(usb::Device&, uint8_t, uint8_t, bool) override { return Error::NotSupported; }
    Error add_device(usb::Device&, uint8_t, usb::Speed) override { return Error::NotSupported; }
    void root_port_changed(uint8_t) override {}
    void remove_device(usb::Device&) override {}
    usb::Device* child(uint8_t, uint8_t) override { return nullptr; }
    usb::Device* device(uint8_t) override { return nullptr; }

    // Delivers one interrupt IN packet, as the host controller would.
    void packet(const uint8_t* data, size_t length) { handler_(context_, data, length); }

private:
    usb::ReportHandler handler_{};
    void* context_{};
};

// LE advertising reports with the given total event size, address, data and
// RSSI. Data: flags, appearance, complete name, then padding.
constexpr uint8_t KEYBOARD_EVENT[] = {
    0x3E, 0x1E, 0x02, 0x01, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0xAA, 0x12,  // 32 bytes: two whole packets
    0x02, 0x01, 0x06, 0x03, 0x19, 0xC1, 0x03, 0x06, 0x09, 'B',  '.',  'O',  '.',  'W', 0x03, 0xFF, 0x00, 0x00, 0xC4,
};
constexpr uint8_t PLAIN_EVENT[] = {0x3E, 0x0C, 0x02, 0x01, 0x04, 0x01, 0x02, 0x00, 0x00, 0x00, 0x00, 0xAA, 0x00, 0xC0};
constexpr uint8_t MOUSE_EVENT[] = {
    0x3E, 0x1F, 0x02, 0x01, 0x00, 0x01, 0x03, 0x00, 0x00, 0x00, 0x00, 0xAA, 0x13,  // 33 bytes, RSSI 0 last
    0x02, 0x01, 0x06, 0x03, 0x19, 0xC2, 0x03, 0x06, 0x09, 'M',  'o',  'u',  's',
    'e',  0x04, 0xFF, 0x00, 0x00, 0x00, 0x00,
};
static_assert(sizeof(KEYBOARD_EVENT) == 32 && sizeof(PLAIN_EVENT) == 14 && sizeof(MOUSE_EVENT) == 33);

constexpr size_t PACKET = 16;  // A full-speed adapter's event endpoint

void send_in_packets(FakeHost& host, const uint8_t* event, size_t length) {
    for (size_t at = 0; at < length; at += PACKET)
        host.packet(event + at, length - at < PACKET ? length - at : PACKET);
}

}  // namespace

extern "C" int main() {
    FakeHost host;
    usb::Device device{};
    device.host = &host;
    device.slot = 1;
    const usb::InterfaceDescriptor interface{9, usb::DESCRIPTOR_INTERFACE, 0, 0, 3, 0xE0, 0x01, 0x01, 0};
    const usb::EndpointDescriptor endpoints[] = {
        {7, usb::DESCRIPTOR_ENDPOINT, 0x81, usb::ENDPOINT_INTERRUPT, PACKET, 1},
        {7, usb::DESCRIPTOR_ENDPOINT, 0x82, 0x02, 64, 0},
        {7, usb::DESCRIPTOR_ENDPOINT, 0x02, 0x02, 64, 0},
    };
    assert(usb::bluetooth::attach(device, interface, endpoints, 3) == Error::None);

    // An event that fills its packets exactly ends with a one-byte zero
    // packet on the VisionFive 2; the next event must still start afresh.
    send_in_packets(host, KEYBOARD_EVENT, sizeof(KEYBOARD_EVENT));
    const uint8_t terminator = 0;
    host.packet(&terminator, 1);
    send_in_packets(host, PLAIN_EVENT, sizeof(PLAIN_EVENT));
    // A zero byte that completes an event is part of it.
    send_in_packets(host, MOUSE_EVENT, sizeof(MOUSE_EVENT));

    const bool keyboard = LogFixture::contains("AA:00:00:00:00:01 LE, keyboard \"B.O.W\"");
    const bool plain = LogFixture::contains("AA:00:00:00:00:02 LE\n");
    const bool mouse = LogFixture::contains("AA:00:00:00:00:03 LE, mouse \"Mouse\"");
    if (!keyboard || !plain || !mouse) {
        printf("missing advertising reports (keyboard %d, plain %d, mouse %d); log:\n%s", keyboard, plain, mouse,
               LogFixture::text());
        return 1;
    }

    usb::bluetooth::detach(device);
    printf("bluetooth contract passed\n");
    return 0;
}
