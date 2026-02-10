#pragma once

#include <base/types.h>

#include "ports.h"

static inline uint8_t inb(uint16_t port) __attribute__((always_inline));
static inline uint8_t inb_p(uint16_t port) __attribute__((always_inline));
static inline uint16_t inw(uint16_t port) __attribute__((always_inline));
static inline uint32_t inl(uint16_t port) __attribute__((always_inline));
static inline void insl(uint32_t port, void *addr, int cnt) __attribute__((always_inline));
static inline void insw(uint32_t port, void *addr, int cnt) __attribute__((always_inline));

static inline void outb(uint16_t port, uint8_t data) __attribute__((always_inline));
static inline void outw(uint16_t port, uint16_t data) __attribute__((always_inline));
static inline void outl(uint16_t port, uint32_t data) __attribute__((always_inline));
static inline void outsw(uint32_t port, const void *addr, int cnt) __attribute__((always_inline));

static inline void outb_p(uint16_t port, uint8_t data) __attribute__((always_inline));

static inline void io_wait(void) __attribute__((always_inline));

static inline uint64_t read_eflags(void) __attribute__((always_inline));
static inline void write_eflags(uint64_t eflags) __attribute__((always_inline));

static inline void lcr0(uintptr_t cr0) __attribute__((always_inline));
static inline void lcr3(uintptr_t cr3) __attribute__((always_inline));

static inline uintptr_t rcr2(void) __attribute__((always_inline));
static inline uintptr_t rcr3(void) __attribute__((always_inline));

static inline void invlpg(void *addr) __attribute__((always_inline));


static inline uint8_t inb(uint16_t port) {
    uint8_t data;
    asm volatile ("inb %1, %0" : "=a" (data) : "d" (port));
    return data;
}

static inline uint8_t inb_p(uint16_t port) {
    uint8_t data;
    asm volatile ("inb %1, %0;"
	    "jmp 1f;"
	    "1:jmp 1f;"
	    "1:" : "=a" (data) : "d" (port));
    return data;
}

static inline uint16_t inw(uint16_t port) {
    uint16_t data;
    asm volatile ("inw %1, %0" : "=a" (data) : "d" (port));
    return data;
}

static inline uint32_t inl(uint16_t port) {
    uint32_t data;
    asm volatile ("inl %1, %0" : "=a" (data) : "d" (port));
    return data;
}

// Read [cnt] dwords to address [addr] from port [port]
static inline void insl(uint32_t port, void *addr, int cnt) {
    asm volatile (
            "cld;"
            "repne; insl;"
            : "=D" (addr), "=c" (cnt)
            : "d" (port), "0" (addr), "1" (cnt)
            : "memory", "cc");
}

// Read [cnt] words to address [addr] from port [port]
static inline void insw(uint32_t port, void *addr, int cnt) {
    asm volatile (
            "cld;"
            "rep; insw;"
            : "=D" (addr), "=c" (cnt)
            : "d" (port), "0" (addr), "1" (cnt)
            : "memory", "cc");
}

static inline void outb(uint16_t port, uint8_t data) {
    asm volatile ("outb %0, %1" :: "a"(data), "d"(port));
}

static inline void outw(uint16_t port, uint16_t data) {
    asm volatile("outw %0, %1" ::"a"(data), "d"(port) : "memory");
}

static inline void outl(uint16_t port, uint32_t data) {
    asm volatile("outl %0, %1" ::"a"(data), "d"(port) : "memory");
}

// Write [cnt] words from address [addr] to port [port]
static inline void outsw(uint32_t port, const void *addr, int cnt) {
    asm volatile (
            "cld;"
            "rep; outsw;"
            : "=S" (addr), "=c" (cnt)
            : "d" (port), "0" (addr), "1" (cnt)
            : "memory", "cc");
}

static inline void outb_p(uint16_t port, uint8_t data) {
    asm volatile ("outb %0, %1;"
		"jmp 1f;"
		"1:jmp 1f;"
		"1:" :: "a"(data), "d"(port));
}

// Short delay using port 0x80 (POST diagnostic port)
static inline void io_wait(void) {
    inb(0x80);
}

static inline uint64_t read_eflags(void) {
    uint64_t eflags;
    asm volatile("pushfq; popq %0" : "=r"(eflags));
    return eflags;
}

static inline void write_eflags(uint64_t eflags) {
    asm volatile("pushq %0; popfq" ::"r"(eflags));
}

static inline void lcr0(uintptr_t cr0) {
    asm volatile("mov %0, %%cr0" ::"r"(cr0) : "memory");
}

static inline void lcr3(uintptr_t cr3) {
    asm volatile("mov %0, %%cr3" ::"r"(cr3) : "memory");
}

static inline uintptr_t rcr2(void) {
    uintptr_t cr2;
    asm volatile("mov %%cr2, %0" : "=r"(cr2)::"memory");
    return cr2;
}

static inline uintptr_t rcr3(void) {
    uintptr_t cr3;
    asm volatile("mov %%cr3, %0" : "=r"(cr3)::"memory");
    return cr3;
}

static inline void invlpg(void *addr) {
    asm volatile("invlpg (%0)" :: "r"(addr) : "memory");
}
