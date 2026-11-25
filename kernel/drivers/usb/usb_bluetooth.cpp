#include "drivers/usb/usb_bluetooth.h"
#include "drivers/bluetooth/hci.h"
#include "drivers/intr.h"
#include "lib/memory.h"
#include <kernel/log.h>
#include <sys/array.hpp>

namespace usb::bluetooth {
namespace {

constexpr uint8_t CLASS_WIRELESS = 0xE0;
constexpr uint8_t SUBCLASS_RF = 0x01;
constexpr uint8_t PROTOCOL_BLUETOOTH = 0x01;

constexpr size_t MAX_COMMAND = 3 + ::bluetooth::Hci::MAX_PARAMETERS;
constexpr size_t MAX_EVENT = 2 + ::bluetooth::Hci::MAX_PARAMETERS;

class Adapter final : public ::bluetooth::Transport {
public:
    Error send_command(const uint8_t* packet, size_t length) override {
        ENSURE(device_ && length <= MAX_COMMAND, Error::Invalid);
        uint8_t data[MAX_COMMAND];
        memcpy(data, packet, length);
        // Commands are class requests to the device, request 0.
        return device_->host->control(*device_, {REQUEST_CLASS, 0, 0, 0, static_cast<uint16_t>(length)}, data);
    }

    // Events longer than the endpoint's packet size arrive in pieces; the
    // event header says when one is whole.
    static void event_report(void* context, const uint8_t* data, size_t length) {
        auto* adapter = static_cast<Adapter*>(context);
        if (!adapter->device_ || length == 0)
            return;
        if (adapter->event_length_ + length > MAX_EVENT) {
            adapter->event_length_ = 0;  // Lost track of the event boundary
            return;
        }
        memcpy(adapter->event_ + adapter->event_length_, data, length);
        adapter->event_length_ += length;
        if (adapter->event_length_ < 2 || adapter->event_length_ < 2U + adapter->event_[1])
            return;
        ::bluetooth::Hci::event_received(*adapter, adapter->event_, adapter->event_length_);
        adapter->event_length_ = 0;
    }

    [[nodiscard]] Device* device() const { return device_; }

    void bind(Device& device) {
        device_ = &device;
        event_length_ = 0;
    }
    void release() { device_ = nullptr; }

private:
    Device* device_{};  // Null while the entry is free
    uint8_t event_[MAX_EVENT]{};
    size_t event_length_{};
};

class Registry {
public:
    static Adapter* add(Device& device) {
        intr::Guard guard;
        for (Adapter& adapter : adapters_) {
            if (adapter.device())
                continue;
            adapter.bind(device);
            return &adapter;
        }
        return nullptr;
    }

    static Adapter* find(Device& device) {
        for (Adapter& adapter : adapters_)
            if (adapter.device() == &device)
                return &adapter;
        return nullptr;
    }

    static void remove(Adapter& adapter) {
        intr::Guard guard;
        adapter.release();
    }

private:
    inline static sys::array<Adapter, ::bluetooth::Hci::MAX_CONTROLLERS> adapters_{};
};

}  // namespace

Error attach(Device& device, const InterfaceDescriptor& interface, const EndpointDescriptor* endpoints,
             size_t endpoint_count) {
    ENSURE(interface.interface_class == CLASS_WIRELESS && interface.interface_subclass == SUBCLASS_RF &&
               interface.interface_protocol == PROTOCOL_BLUETOOTH,
           Error::NotSupported);
    // The HCI interface has the event endpoint; the voice interface beside it
    // has only isochronous ones.
    const EndpointDescriptor* events = nullptr;
    for (size_t i = 0; i < endpoint_count; ++i)
        if ((endpoints[i].address & ENDPOINT_IN) != 0 &&
            (endpoints[i].attributes & ENDPOINT_TRANSFER_MASK) == ENDPOINT_INTERRUPT)
            events = &endpoints[i];
    ENSURE(events, Error::NotSupported);

    Adapter* adapter = Registry::add(device);
    ENSURE_LOG(adapter, Error::Full, "usb %s: too many Bluetooth adapters", location(device).text);
    Error error = device.host->start_interrupt_in(device, *events, Adapter::event_report, adapter);
    if (error == Error::None) {
        klog::info("usb %s: bluetooth adapter\n", location(device).text);
        error = ::bluetooth::Hci::attach(*adapter);
    }
    if (error != Error::None)
        Registry::remove(*adapter);
    return error;
}

void detach(Device& device) {
    Adapter* adapter = Registry::find(device);
    if (!adapter)
        return;
    ::bluetooth::Hci::detach(*adapter);
    Registry::remove(*adapter);
}

}  // namespace usb::bluetooth
