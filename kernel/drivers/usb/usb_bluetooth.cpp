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
constexpr size_t ACL_HEADER = 4;  // Handle and flags, data length
constexpr size_t MAX_ACL = ACL_HEADER + 1024;

class Adapter final : public ::bluetooth::Transport {
public:
    Error send_command(uint8_t* packet, size_t length) override {
        ENSURE(device_ && length <= MAX_COMMAND, Error::Invalid);
        // Commands are class requests to the device, request 0.
        return device_->host->control(*device_, {REQUEST_CLASS, 0, 0, 0, static_cast<uint16_t>(length)}, packet);
    }

    Error send_acl(uint8_t* packet, size_t length) override {
        ENSURE(device_, Error::Invalid);
        return device_->host->send(*device_, acl_out_, packet, length);
    }

    // Events longer than the endpoint's packet size arrive in pieces; the
    // event header says when one is whole.
    static void event_report(void* context, const uint8_t* data, size_t length) {
        auto* adapter = static_cast<Adapter*>(context);
        if (!adapter->device_ || length == 0)
            return;
        // An event that fills its last packet is ended by an empty packet;
        // on the VisionFive 2 (33FA:0012 behind a VL805) that arrives as one
        // zero byte. No event has code 0, so such a byte between events only
        // ends the previous one.
        if (adapter->event_length_ == 0 && length == 1 && data[0] == 0)
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

    // ACL packets are put together the same way from the bulk IN endpoint,
    // by the length in their header.
    static void acl_report(void* context, const uint8_t* data, size_t length) {
        auto* adapter = static_cast<Adapter*>(context);
        if (!adapter->device_ || length == 0)
            return;
        // A packet cannot start with less than its header: such a piece
        // between packets is the end marker of the one before.
        if (adapter->acl_length_ == 0 && length < ACL_HEADER)
            return;
        if (adapter->acl_length_ + length > MAX_ACL) {
            adapter->acl_length_ = 0;
            return;
        }
        memcpy(adapter->acl_ + adapter->acl_length_, data, length);
        adapter->acl_length_ += length;
        const size_t whole = ACL_HEADER + static_cast<size_t>(adapter->acl_[2] | (adapter->acl_[3] << 8));
        if (adapter->acl_length_ < whole)
            return;
        ::bluetooth::Hci::acl_received(*adapter, adapter->acl_, whole);
        adapter->acl_length_ = 0;
    }

    [[nodiscard]] Device* device() const { return device_; }

    void bind(Device& device, uint8_t acl_out) {
        device_ = &device;
        acl_out_ = acl_out;
        event_length_ = 0;
        acl_length_ = 0;
    }
    void release() { device_ = nullptr; }

private:
    Device* device_{};  // Null while the entry is free
    uint8_t acl_out_{};
    uint8_t event_[MAX_EVENT]{};
    size_t event_length_{};
    uint8_t acl_[MAX_ACL]{};
    size_t acl_length_{};
};

class Registry {
public:
    static Adapter* add(Device& device, uint8_t acl_out) {
        intr::Guard guard;
        for (Adapter& adapter : adapters_) {
            if (adapter.device())
                continue;
            adapter.bind(device, acl_out);
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
    // The HCI interface has the event endpoint and the ACL data endpoints;
    // the voice interface beside it has only isochronous ones.
    const EndpointDescriptor* events = nullptr;
    const EndpointDescriptor* acl_in = nullptr;
    const EndpointDescriptor* acl_out = nullptr;
    for (size_t i = 0; i < endpoint_count; ++i) {
        const uint8_t transfer = endpoints[i].attributes & ENDPOINT_TRANSFER_MASK;
        const bool in = (endpoints[i].address & ENDPOINT_IN) != 0;
        if (in && transfer == ENDPOINT_INTERRUPT)
            events = &endpoints[i];
        else if (in && transfer == ENDPOINT_BULK)
            acl_in = &endpoints[i];
        else if (!in && transfer == ENDPOINT_BULK)
            acl_out = &endpoints[i];
    }
    ENSURE(events && acl_in && acl_out, Error::NotSupported);

    Adapter* adapter = Registry::add(device, acl_out->address);
    ENSURE_LOG(adapter, Error::Full, "usb %s: too many Bluetooth adapters", location(device).text);
    Error error = device.host->start_in(device, *events, Adapter::event_report, adapter);
    if (error == Error::None)
        error = device.host->start_in(device, *acl_in, Adapter::acl_report, adapter);
    if (error == Error::None)
        error = device.host->open_out(device, *acl_out);
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
