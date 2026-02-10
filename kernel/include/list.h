#pragma once

#include <base/types.h>

// Intrusive Data Structure

struct ListNode {
    ListNode* m_prev{};
    ListNode* m_next{};

    inline void init() {
        m_prev = m_next = this;
    }

    inline ListNode* get_next() const {
        return m_next;
    }

    inline ListNode* get_prev() const {
        return m_prev;
    }

    inline void add_before(ListNode& elm) {
        elm.m_prev = m_prev;
        elm.m_next = this;
        m_prev->m_next = &elm;
        m_prev = &elm;
    }

    inline void add_after(ListNode& elm) {
        elm.m_prev = this;
        elm.m_next = m_next;
        m_next->m_prev = &elm;
        m_next = &elm;
    }

    inline void add(ListNode& elm) {
        add_after(elm);
    }

    inline void unlink() {
        m_prev->m_next = m_next;
        m_next->m_prev = m_prev;
    }

    [[nodiscard]] inline bool empty() const {
        return m_next == this;
    }

    template<typename T>
    inline T* container() const {
        return reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(this) - T::node_offset());
    }

    template<typename T>
    inline void add_before(T* obj) {
        add_before(obj->node());
    }
};
