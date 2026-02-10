#include "kdb.h"

#ifdef CONFIG_PS2KBD

#include <arch/x86/drivers/i8042.h>
#include <arch/x86/drivers/i8259.h>

#include <base/types.h>
#include <arch/x86/io.h>

#include "pic.h"

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
    if ((inb(KBD_STATUS_REG) & KBD_OBF_FULL) == 0)
        return -1;

    uint8_t data = inb(KBD_DATA_REG);
    
    // Ignore key release events (scancode & 0x80)
    if (data & 0x80)
        return -1;
    
    return normal_map[data];
}

} // namespace kbd

#endif // CONFIG_PS2KBD