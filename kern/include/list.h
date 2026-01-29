#pragma once

struct list_entry_t {
    list_entry_t* prev{};
    list_entry_t* next{};
};

static inline __attribute__((always_inline)) void __list_add(list_entry_t *elm, list_entry_t *prev, list_entry_t *next) {
    prev->next = next->prev = elm;
    elm->next = next;
    elm->prev = prev;
}

static inline __attribute__((always_inline)) void __list_del(list_entry_t* prev, list_entry_t* next) {
    prev->next = next;
    next->prev = prev;
}

static inline void list_init(list_entry_t* l) {
    l->prev = l->next = l;
}

static inline  __attribute__((always_inline)) list_entry_t* list_next(list_entry_t* l){
    return l->next;
}

static inline  __attribute__((always_inline)) list_entry_t* list_prev(list_entry_t* l){
    return l->prev;
}

static inline __attribute__((always_inline)) void list_add_before(list_entry_t* l, list_entry_t* elm) {
    __list_add(elm, l->prev, l);
}

static inline __attribute__((always_inline)) void list_add_after(list_entry_t* l, list_entry_t* elm) {
    __list_add(elm, l, l->next);
}

static inline __attribute__((always_inline)) void list_del(list_entry_t* l) {
    __list_del(l->prev, l->next);
}

static inline __attribute__((always_inline)) void list_add(list_entry_t* l, list_entry_t* elm) {
    list_add_after(l, elm);
}