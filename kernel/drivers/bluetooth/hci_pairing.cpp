// LE legacy pairing as the initiator, with Just Works (neither side shows or
// enters a passkey), and link encryption. The AES-128 the pairing functions
// need comes from the controller (LE Encrypt), as do random numbers.
#include "drivers/bluetooth/hci.h"
#include "drivers/bluetooth/hci_defs.h"
#include "drivers/intr.h"
#include "lib/memory.h"
#include <kernel/log.h>

namespace bluetooth {
namespace {

constexpr uint8_t SMP_PAIRING_REQUEST = 0x01;
constexpr uint8_t SMP_PAIRING_RESPONSE = 0x02;
constexpr uint8_t SMP_PAIRING_CONFIRM = 0x03;
constexpr uint8_t SMP_PAIRING_RANDOM = 0x04;
constexpr uint8_t SMP_PAIRING_FAILED = 0x05;
constexpr uint8_t SMP_ENCRYPTION_INFORMATION = 0x06;
constexpr uint8_t SMP_CENTRAL_IDENTIFICATION = 0x07;
constexpr uint8_t SMP_CONFIRM_VALUE_FAILED = 0x04;

constexpr size_t PAIRING_FEATURES = 7;  // Code, I/O capability, OOB, auth, key size, key distributions
constexpr uint8_t IO_NO_INPUT_NO_OUTPUT = 0x03;
constexpr uint8_t AUTH_BONDING = 0x01;
constexpr uint8_t MAX_KEY_SIZE = 16;
constexpr uint8_t MIN_KEY_SIZE = 7;
constexpr uint8_t KEY_DISTRIBUTION_ENCRYPTION = 0x01;  // LTK, EDIV and Rand
constexpr size_t PAIRING_KEY_SIZE = 4;                 // Offset of the key size in the features
constexpr size_t RESPONDER_KEYS = 6;                   // Offset of the responder's key distribution
constexpr uint64_t SMP_TIMEOUT_MS = 30000;
constexpr uint64_t ENCRYPTION_TIMEOUT_MS = 10000;
constexpr uint8_t ADDRESS_PUBLIC = 0;

// Pairing Request: no I/O (Just Works), bonding, the largest key, and from
// the keyboard only its encryption key, which reconnecting needs.
constexpr uint8_t PAIRING_REQUEST[PAIRING_FEATURES] = {
    SMP_PAIRING_REQUEST, IO_NO_INPUT_NO_OUTPUT, 0, AUTH_BONDING, MAX_KEY_SIZE, 0, KEY_DISTRIBUTION_ENCRYPTION};

// The c1 example from the core specification (Vol 3 Part H, D.1), least
// significant byte first: checks that LE Encrypt is used the right way round.
constexpr uint8_t C1_RANDOM[KEY_BYTES] = {0xE0, 0x2E, 0x70, 0xC6, 0x4E, 0x27, 0x88, 0x63,
                                          0x0E, 0x6F, 0xAD, 0x56, 0x21, 0xD5, 0x83, 0x57};
constexpr uint8_t C1_REQUEST[PAIRING_FEATURES] = {0x01, 0x01, 0x00, 0x00, 0x10, 0x07, 0x07};
constexpr uint8_t C1_RESPONSE[PAIRING_FEATURES] = {0x02, 0x03, 0x00, 0x00, 0x08, 0x00, 0x05};
constexpr uint8_t C1_INITIATOR[ADDRESS_BYTES] = {0xA6, 0xA5, 0xA4, 0xA3, 0xA2, 0xA1};
constexpr uint8_t C1_RESPONDER[ADDRESS_BYTES] = {0xB6, 0xB5, 0xB4, 0xB3, 0xB2, 0xB1};
constexpr uint8_t C1_EXPECTED[KEY_BYTES] = {0x86, 0x3B, 0xF1, 0xBE, 0xC5, 0x4D, 0xA7, 0xD2,
                                            0xEA, 0x88, 0x89, 0x87, 0xEF, 0x3F, 0x1E, 0x1E};

constexpr uint8_t ZERO_KEY[KEY_BYTES] = {};  // Just Works' temporary key

}  // namespace

// e(key, plaintext): AES-128 by the controller. Values are least significant
// byte first, as HCI carries them.
Error Hci::aes(Controller& controller, const uint8_t* key, const uint8_t* plaintext, uint8_t* out) {
    uint8_t parameters[2 * KEY_BYTES];
    memcpy(parameters, key, KEY_BYTES);
    memcpy(parameters + KEY_BYTES, plaintext, KEY_BYTES);
    Reply reply{};
    TRY(command(controller, OP_LE_ENCRYPT, parameters, sizeof(parameters), reply));
    ENSURE(reply.length >= 1 + KEY_BYTES, Error::Io);
    memcpy(out, reply.data + 1, KEY_BYTES);
    return Error::None;
}

Error Hci::random(Controller& controller, uint8_t* out, size_t length) {
    for (size_t used = 0; used < length;) {
        Reply reply{};
        TRY(command(controller, OP_LE_RAND, nullptr, 0, reply));
        ENSURE(reply.length >= 9, Error::Io);
        const size_t take = length - used < 8 ? length - used : 8;
        memcpy(out + used, reply.data + 1, take);
        used += take;
    }
    return Error::None;
}

// c1, the legacy pairing confirm value, with Just Works' zero key:
// e(k, e(k, r XOR p1) XOR p2), p1 = response || request || responder type
// || initiator type and p2 = 0 || initiator || responder.
Error Hci::confirm_value(Controller& controller, const uint8_t* random, const uint8_t* request, const uint8_t* response,
                         uint8_t initiator_type, const uint8_t* initiator, uint8_t responder_type,
                         const uint8_t* responder, uint8_t* out) {
    uint8_t value[KEY_BYTES];
    value[0] = initiator_type;
    value[1] = responder_type;
    memcpy(value + 2, request, PAIRING_FEATURES);
    memcpy(value + 2 + PAIRING_FEATURES, response, PAIRING_FEATURES);
    for (size_t i = 0; i < KEY_BYTES; ++i)
        value[i] ^= random[i];
    TRY(aes(controller, ZERO_KEY, value, value));
    uint8_t padding[KEY_BYTES]{};
    memcpy(padding, responder, ADDRESS_BYTES);
    memcpy(padding + ADDRESS_BYTES, initiator, ADDRESS_BYTES);
    for (size_t i = 0; i < KEY_BYTES; ++i)
        value[i] ^= padding[i];
    return aes(controller, ZERO_KEY, value, out);
}

void Hci::check_aes(Controller& controller) {
    uint8_t value[KEY_BYTES];
    const Error error =
        confirm_value(controller, C1_RANDOM, C1_REQUEST, C1_RESPONSE, 1, C1_INITIATOR, 0, C1_RESPONDER, value);
    if (error == Error::None && memcmp(value, C1_EXPECTED, KEY_BYTES) != 0)
        klog::warn("bluetooth hci%u: LE Encrypt does not match the specification's example\n", controller.index);
}

// Starts encryption with `ltk`, then waits for the controller to report it on.
// Access when the keyboard refuses the key.
Error Hci::encrypt(Controller& controller, const uint8_t* ltk, uint16_t ediv, const uint8_t* rand) {
    uint8_t parameters[2 + 8 + 2 + KEY_BYTES];
    {
        intr::Guard guard;
        ENSURE(controller.link.connected, Error::Interrupted);
        write16(parameters, controller.link.handle);
    }
    memcpy(parameters + 2, rand, 8);
    write16(parameters + 10, ediv);
    memcpy(parameters + 12, ltk, KEY_BYTES);
    Reply reply{};
    expect_event(controller, EVENT_ENCRYPTION_CHANGE);
    Error error = command(controller, OP_LE_ENABLE_ENCRYPTION, parameters, sizeof(parameters), reply);
    if (error == Error::None)
        error = wait_event(controller, ENCRYPTION_TIMEOUT_MS, reply);
    else
        expect_event(controller, 0);
    TRY(error);
    // Status, handle, enabled.
    ENSURE(reply.length >= 4, Error::Io);
    ENSURE_LOG(reply.data[0] == STATUS_SUCCESS && reply.data[3] != 0, Error::Access,
               "bluetooth hci%u: encryption refused (status 0x%02x)", controller.index, reply.data[0]);
    klog::info("bluetooth hci%u: link encrypted\n", controller.index);
    return Error::None;
}

Error Hci::pair(Controller& controller, const Peer& keyboard) {
    TRY(send_frame(controller, CHANNEL_SMP, PAIRING_REQUEST, sizeof(PAIRING_REQUEST)));
    // The keyboard may ask for security first; its answer to the request
    // follows.
    Frame frame{};
    do {
        TRY(wait_frame(controller, CHANNEL_SMP, SMP_TIMEOUT_MS, frame));
        ENSURE_LOG(frame.data[0] != SMP_PAIRING_FAILED, Error::Access,
                   "bluetooth hci%u: the keyboard refused pairing (reason 0x%02x)", controller.index,
                   frame.length >= 2 ? frame.data[1] : 0);
    } while (frame.data[0] != SMP_PAIRING_RESPONSE);
    ENSURE(frame.length >= PAIRING_FEATURES, Error::Io);
    uint8_t response[PAIRING_FEATURES];
    memcpy(response, frame.data, PAIRING_FEATURES);
    const uint8_t key_size = response[PAIRING_KEY_SIZE] < MAX_KEY_SIZE ? response[PAIRING_KEY_SIZE] : MAX_KEY_SIZE;
    ENSURE(key_size >= MIN_KEY_SIZE, Error::NotSupported);

    // Exchange confirm values, then the randoms behind them, and check the
    // keyboard's.
    uint8_t local_random[KEY_BYTES];
    TRY(random(controller, local_random, KEY_BYTES));
    uint8_t message[1 + KEY_BYTES] = {SMP_PAIRING_CONFIRM};
    TRY(confirm_value(controller, local_random, PAIRING_REQUEST, response, ADDRESS_PUBLIC, controller.address,
                      keyboard.address_type, keyboard.address, message + 1));
    TRY(send_frame(controller, CHANNEL_SMP, message, sizeof(message)));
    TRY(wait_frame(controller, CHANNEL_SMP, SMP_TIMEOUT_MS, frame));
    ENSURE_LOG(frame.data[0] == SMP_PAIRING_CONFIRM && frame.length >= 1 + KEY_BYTES, Error::Access,
               "bluetooth hci%u: pairing stopped (0x%02x)", controller.index, frame.data[0]);
    uint8_t remote_confirm[KEY_BYTES];
    memcpy(remote_confirm, frame.data + 1, KEY_BYTES);
    message[0] = SMP_PAIRING_RANDOM;
    memcpy(message + 1, local_random, KEY_BYTES);
    TRY(send_frame(controller, CHANNEL_SMP, message, sizeof(message)));
    TRY(wait_frame(controller, CHANNEL_SMP, SMP_TIMEOUT_MS, frame));
    ENSURE_LOG(frame.data[0] == SMP_PAIRING_RANDOM && frame.length >= 1 + KEY_BYTES, Error::Access,
               "bluetooth hci%u: pairing stopped (0x%02x)", controller.index, frame.data[0]);
    uint8_t remote_random[KEY_BYTES];
    memcpy(remote_random, frame.data + 1, KEY_BYTES);
    uint8_t expected[KEY_BYTES];
    TRY(confirm_value(controller, remote_random, PAIRING_REQUEST, response, ADDRESS_PUBLIC, controller.address,
                      keyboard.address_type, keyboard.address, expected));
    if (memcmp(expected, remote_confirm, KEY_BYTES) != 0) {
        const uint8_t failed[2] = {SMP_PAIRING_FAILED, SMP_CONFIRM_VALUE_FAILED};
        static_cast<void>(send_frame(controller, CHANNEL_SMP, failed, sizeof(failed)));
        klog::warn("bluetooth hci%u: the keyboard's confirm value does not match\n", controller.index);
        return Error::Access;
    }

    // s1: the short-term key is e(k, the low halves of the keyboard's random
    // then ours), cut to the agreed size.
    uint8_t short_term_key[KEY_BYTES];
    memcpy(short_term_key, local_random, 8);
    memcpy(short_term_key + 8, remote_random, 8);
    TRY(aes(controller, ZERO_KEY, short_term_key, short_term_key));
    memset(short_term_key + key_size, 0, KEY_BYTES - key_size);
    const uint8_t no_rand[8] = {};
    TRY(encrypt(controller, short_term_key, 0, no_rand));

    // Over the encrypted link the keyboard hands out its long-term key.
    Bond bond{};
    memcpy(bond.address, keyboard.address, ADDRESS_BYTES);
    bond.address_type = keyboard.address_type;
    bool have_key = false;
    bool have_identity = false;
    while ((response[RESPONDER_KEYS] & KEY_DISTRIBUTION_ENCRYPTION) != 0 && !(have_key && have_identity)) {
        TRY(wait_frame(controller, CHANNEL_SMP, SMP_TIMEOUT_MS, frame));
        if (frame.data[0] == SMP_ENCRYPTION_INFORMATION && frame.length >= 1 + KEY_BYTES) {
            memcpy(bond.ltk, frame.data + 1, KEY_BYTES);
            have_key = true;
        } else if (frame.data[0] == SMP_CENTRAL_IDENTIFICATION && frame.length >= 11) {
            bond.ediv = read16(frame.data + 1);
            memcpy(bond.rand, frame.data + 3, 8);
            have_identity = true;
        }
    }
    intr::Guard guard;
    bond.valid = have_key && have_identity;
    controller.bond = bond;
    klog::info("bluetooth hci%u: paired with %s%s\n", controller.index, address_text(keyboard.address).text,
               bond.valid ? "" : " (no key for reconnecting)");
    return Error::None;
}

}  // namespace bluetooth
