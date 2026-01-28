#include "kdb.h"

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
    [0xC7] KEY_HOME, [0x9C] KP_ENTER,
    [0xB5] KP_DIV  , [0xC8] KEY_UP,
    [0xC9] KEY_PGUP, [0xCB] KEY_LF,
    [0xCD] KEY_RT  , [0xCF] KEY_END,
    [0xD0] KEY_DN  , [0xD1] KEY_PGDN,
    [0xD2] KEY_INS , [0xD3] KEY_DEL
};

void kbd_init(void) {
    pic_enable(IRQ_KBD);
}

int kdb_getc(void) {
    if ((inb(KBD_STATUS_REG) & KBD_OBF_FULL) == 0)
        return -1;

    uint8_t data = inb(KBD_DATA_REG);
    
    // Ignore key release events (scancode & 0x80)
    if (data & 0x80)
        return -1;
    
    return normal_map[data];
}