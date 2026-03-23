#pragma once

#include <base/types.h>

// Intrusive Data Structure

namespace list_detail {

template<typename NodePtr, bool Reverse = false>
struct Iterator {
    NodePtr cur{};

    NodePtr operator*() const { return cur; }

    Iterator& operator++() {
        if constexpr (Reverse) {
            cur = cur->prev;
        } else {
            cur = cur->next;
        }
        return *this;
    }

    bool operator==(const Iterator& other) const { return cur == other.cur; }
    bool operator!=(const Iterator& other) const { return cur != other.cur; }
};

template<typename NodePtr, typename Iter>
struct ReverseView {
    NodePtr head{};

    [[nodiscard]] Iter begin() const { return Iter{head->prev}; }
    [[nodiscard]] Iter end() const { return Iter{head}; }
};

template<typename NodePtr>
struct CircularIterator {
    NodePtr head{};
    NodePtr start{};
    NodePtr cur{};

    NodePtr operator*() const { return cur; }

    CircularIterator& operator++() {
        if (!cur) {
            return *this;
        }

        NodePtr next = cur->next;
        if (next == head) {
            next = next->next;
        }

        if (next == head || next == start) {
            cur = nullptr;
        } else {
            cur = next;
        }
        return *this;
    }

    bool operator==(const CircularIterator& other) const { return cur == other.cur; }
    bool operator!=(const CircularIterator& other) const { return cur != other.cur; }
};

template<typename NodePtr, typename Iter>
struct CircularView {
    NodePtr head{};
    NodePtr start{};

    [[nodiscard]] Iter begin() const {
        if (!head) {
            return end();
        }

        NodePtr first = start ? start : head->next;
        if (first == head) {
            first = first->next;
        }
        if (first == head) {
            return end();
        }
        return Iter{head, first, first};
    }

    [[nodiscard]] Iter end() const { return Iter{head, start, nullptr}; }
};

}  // namespace list_detail

struct ListNode {
    using iterator = list_detail::Iterator<ListNode*>;
    using reverse_iterator = list_detail::Iterator<ListNode*, true>;
    using circular_iterator = list_detail::CircularIterator<ListNode*>;

    using reverse_view = list_detail::ReverseView<ListNode*, reverse_iterator>;
    using circular_view = list_detail::CircularView<ListNode*, circular_iterator>;

    ListNode* prev{};
    ListNode* next{};

    ListNode() { prev = next = this; }

    iterator begin() { return iterator{next}; }
    iterator end() { return iterator{this}; }

    [[nodiscard]] reverse_view reversed() { return reverse_view{this}; }
    [[nodiscard]] circular_view circular_from(ListNode* start) { return circular_view{this, start}; }

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
