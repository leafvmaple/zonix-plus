#ifndef ZONIX_ABI_DISPLAY_H
#define ZONIX_ABI_DISPLAY_H

#define DISPLAY_GET_INFO   0x5701
#define DISPLAY_ACQUIRE    0x5702
#define DISPLAY_RELEASE    0x5703
#define DISPLAY_PRESENT    0x5704
#define DISPLAY_READ_EVENT 0x5705
#define DISPLAY_READ_TIMED_EVENT 0x5706

#define DISPLAY_ACQUIRE_EVENTS          1
#define DISPLAY_ACQUIRE_KEYBOARD_CTRL_C 2

/* Pixels are 32-bit 0x00RRGGBB words; stride is in bytes, not pixels.
 * Device addresses and the native scanout layout are never exposed. */
#define DISPLAY_FORMAT_XRGB8888 1

struct DisplayInfo {
    unsigned int width;
    unsigned int height;
    unsigned int stride;
    unsigned int format;
};

struct DisplayPresent {
    unsigned int x;
    unsigned int y;
    unsigned int width;
    unsigned int height;
    unsigned int stride;
    unsigned int reserved;
    long pixels;
};

/* Events are delivered only to the foreground display session that opts in.
 * Key codes follow the Linux evdev numbering used by VirtIO input. */
#define DISPLAY_EVENT_RESET  0
#define DISPLAY_EVENT_KEY    1
#define DISPLAY_EVENT_BUTTON 2
#define DISPLAY_EVENT_REL    3

#define DISPLAY_KEY_RELEASE 0
#define DISPLAY_KEY_PRESS   1
#define DISPLAY_KEY_REPEAT  2

#define DISPLAY_KEY_ESCAPE 1
#define DISPLAY_KEY_Q      16
#define DISPLAY_KEY_SPACE  57

#define DISPLAY_BUTTON_LEFT   272
#define DISPLAY_BUTTON_RIGHT  273
#define DISPLAY_BUTTON_MIDDLE 274

#define DISPLAY_REL_X     0
#define DISPLAY_REL_Y     1
#define DISPLAY_REL_WHEEL 8

#define DISPLAY_MOD_SHIFT 1
#define DISPLAY_MOD_CTRL  2
#define DISPLAY_MOD_ALT   4
#define DISPLAY_MOD_META  8

struct DisplayEvent {
    unsigned int type;
    unsigned int code;
    int value;
    unsigned int modifiers;
};

/* The monotonic timestamp is recorded when the event is queued. Both read
 * requests consume the same event stream. */
struct DisplayTimedEvent {
    struct DisplayEvent event;
    unsigned long timestamp_ms;
};

#endif /* ZONIX_ABI_DISPLAY_H */
