#include "kdb.h"

#ifdef CONFIG_PS2KBD

#include <asm/drivers/i8042.h>
#include <asm/drivers/i8259.h>

#include <base/types.h>
#include <asm/arch.h>

#include "pic.h"
#include "intr.h"
#include "../sched/sched.h"

// ============================================================================
// Input ring buffer — filled by IRQ, drained by getc_blocking()
// ============================================================================
namespace {

constexpr size_t KBD_BUF_SIZE = 128;

char kbd_buf[KBD_BUF_SIZE];
volatile int kbd_read  = 0;
volatile int kbd_write = 0;

TaskStruct* kbd_waiter = nullptr;   // process sleeping on input

} // namespace

static uint8_t normal_map[256] = {
    NO  , 0x1B, '1', '2' , '3' , '4', '5' , '6' ,  // 0x00
    '7' , '8' , '9', '0' , '-' , '=', '\b', '\t',
    'q' , 'w' , 'e', 'r' , 't' , 'y', 'u' , 'i' ,  // 0x10
    'o' , 'p' , '[', ']' , '\n', NO , 'a' , 's' ,
    'd' , 'f' , 'g', 'h' , 'j' , 'k', 'l' , ';' ,  // 0x20
    '\'', '`' , NO , '\\', 'z' , 'x', 'c' , 'v' ,
    'b' , 'n' , 'm', ',' , '.' , '/', NO  , '*' ,  // 0x30
    NO  , ' ' , NO , NO  , NO  , NO , NO  , NO  ,
    NO  , NO  , NO , NO  , NO  , NO , NO  , '7' ,  // 0x40
    '8' , '9' , '-', '4' , '5' , '6', '+' , '1' ,
    '2' , '3' , '0', '.' , NO  , NO , NO  , NO  ,  // 0x50
};

static bool normal_map_initialized = false;

static void init_normal_map(void) {
    if (normal_map_initialized) {
        return;
    }

    normal_map_initialized = true;
    normal_map[0xC7] = KEY_HOME;
    normal_map[0x9C] = KP_ENTER;
    normal_map[0xB5] = KP_DIV;
    normal_map[0xC8] = KEY_UP;
    normal_map[0xC9] = KEY_PGUP;
    normal_map[0xCB] = KEY_LF;
    normal_map[0xCD] = KEY_RT;
    normal_map[0xCF] = KEY_END;
    normal_map[0xD0] = KEY_DN;
    normal_map[0xD1] = KEY_PGDN;
    normal_map[0xD2] = KEY_INS;
    normal_map[0xD3] = KEY_DEL;
}

namespace kbd {

void init(void) {
    init_normal_map();
    pic::enable(IRQ_KBD);
}

int getc(void) {
    init_normal_map();
    if ((arch_port_inb(KBD_STATUS_REG) & KBD_OBF_FULL) == 0)
        return -1;

    uint8_t data = arch_port_inb(KBD_DATA_REG);
    
    // Ignore key release events (scancode & 0x80)
    if (data & 0x80)
        return -1;
    
    return normal_map[data];
}

// Called from keyboard IRQ handler — read char from hardware, buffer it
void intr(void) {
    int c = getc();
    if (c <= 0) return;

    int next = (kbd_write + 1) % KBD_BUF_SIZE;
    if (next == kbd_read) return;   // buffer full, drop

    kbd_buf[kbd_write] = (char)c;
    kbd_write = next;

    // Wake up process sleeping in getc_blocking()
    if (kbd_waiter) {
        kbd_waiter->wakeup();
        kbd_waiter = nullptr;
    }
}

// Blocking read — called from process context, sleeps until input available
char getc_blocking(void) {
    while (true) {
        {
            InterruptsGuard guard;
            if (kbd_read != kbd_write) {
                char c = kbd_buf[kbd_read];
                kbd_read = (kbd_read + 1) % KBD_BUF_SIZE;
                return c;
            }
            // Nothing available — sleep
            TaskStruct* current = TaskManager::get_current();
            kbd_waiter = current;
            current->m_state = ProcessState::Sleeping;
        }
        TaskManager::schedule();
    }
}

} // namespace kbd

#endif // CONFIG_PS2KBD