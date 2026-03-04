#pragma once

#include <base/types.h>

// Intrusive Data Structure

struct ListNode {
    ListNode* prev{};
    ListNode* next{};

    inline void init() { prev = next = this; }

    [[nodiscard]] inline ListNode* get_next() const { return next; }
    [[nodiscard]] inline ListNode* get_prev() const { return prev; }

    inline void add_before(ListNode& elm) {
        elm.prev = prev;
        elm.next = this;
        prev->next = &elm;
        prev = &elm;
    }

    inline void add_after(ListNode& elm) {
        elm.prev = this;
        elm.next = next;
        next->prev = &elm;
        next = &elm;
    }

    inline void add(ListNode& elm) { add_after(elm); }

    inline void unlink() const {
        prev->next = next;
        next->prev = prev;
    }

    [[nodiscard]] inline bool empty() const { return next == this; }

    template<typename T>
    [[nodiscard]] inline T* container() const {
        return reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(this) - T::node_offset());
    }

    template<typename T>
    inline void add_before(T* obj) {
        add_before(obj->node());
    }
};
