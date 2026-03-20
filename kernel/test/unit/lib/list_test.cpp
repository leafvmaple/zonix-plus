#include "test/test_defs.h"
#include "lib/list.h"

static int tests_passed = 0;
static int tests_failed = 0;

// ============================================================================
// Helper: count nodes in list (excluding head sentinel)
// ============================================================================

static int list_count(ListNode* head) {
    int n = 0;
    for (auto* le = head->get_next(); le != head; le = le->get_next())
        n++;
    return n;
}

// ============================================================================
// Init and Empty
// ============================================================================

static void test_init_and_empty() {
    TEST_START("ListNode Init & Empty");

    ListNode head;
    head.init();

    TEST_ASSERT(head.empty(), "Freshly initialized list is empty");
    TEST_ASSERT(head.get_next() == &head, "next points to self");
    TEST_ASSERT(head.get_prev() == &head, "prev points to self");
    TEST_ASSERT(list_count(&head) == 0, "Count is 0");

    TEST_END();
}

// ============================================================================
// add (add_after)
// ============================================================================

static void test_add() {
    TEST_START("ListNode add (add_after)");

    ListNode head;
    head.init();

    ListNode a, b, c;
    head.add(a);

    TEST_ASSERT(!head.empty(), "List not empty after add");
    TEST_ASSERT(head.get_next() == &a, "head->next is a");
    TEST_ASSERT(a.get_next() == &head, "a->next is head");
    TEST_ASSERT(list_count(&head) == 1, "Count is 1");

    head.add(b);
    TEST_ASSERT(head.get_next() == &b, "head->next is b after second add");
    TEST_ASSERT(b.get_next() == &a, "b->next is a");
    TEST_ASSERT(list_count(&head) == 2, "Count is 2");

    head.add(c);
    TEST_ASSERT(list_count(&head) == 3, "Count is 3");

    TEST_END();
}

// ============================================================================
// add_before
// ============================================================================

static void test_add_before() {
    TEST_START("ListNode add_before");

    ListNode head;
    head.init();

    ListNode a, b, c;

    head.add_before(a);
    TEST_ASSERT(head.get_prev() == &a, "a is before head (tail)");
    TEST_ASSERT(head.get_next() == &a, "a is also after head (only element)");

    head.add_before(b);
    TEST_ASSERT(head.get_prev() == &b, "b is now tail");
    TEST_ASSERT(a.get_next() == &b, "a->next is b");

    head.add_before(c);
    TEST_ASSERT(head.get_prev() == &c, "c is now tail");
    TEST_ASSERT(list_count(&head) == 3, "Count is 3");

    TEST_END();
}

// ============================================================================
// unlink
// ============================================================================

static void test_unlink() {
    TEST_START("ListNode unlink");

    ListNode head;
    head.init();

    ListNode a, b, c;
    head.add(a);
    head.add(b);
    head.add(c);

    TEST_ASSERT(list_count(&head) == 3, "Start with 3 nodes");

    b.unlink();
    TEST_ASSERT(list_count(&head) == 2, "After unlink b, count is 2");
    TEST_ASSERT(c.get_next() == &a, "c->next is a (b removed)");

    a.unlink();
    TEST_ASSERT(list_count(&head) == 1, "After unlink a, count is 1");
    TEST_ASSERT(head.get_next() == &c, "Only c remains");

    c.unlink();
    TEST_ASSERT(head.empty(), "List empty after removing all");

    TEST_END();
}

// ============================================================================
// Traversal order: add_before builds FIFO order
// ============================================================================

static void test_fifo_order() {
    TEST_START("ListNode FIFO order via add_before");

    ListNode head;
    head.init();

    ListNode nodes[5];
    for (int i = 0; i < 5; i++) {
        head.add_before(nodes[i]);
    }

    int idx = 0;
    bool correct_order = true;
    for (auto* le = head.get_next(); le != &head; le = le->get_next()) {
        if (le != &nodes[idx]) {
            correct_order = false;
            break;
        }
        idx++;
    }

    TEST_ASSERT(correct_order && idx == 5, "add_before produces FIFO traversal order");

    TEST_END();
}

// ============================================================================
// Traversal order: add (add_after) builds LIFO order
// ============================================================================

static void test_lifo_order() {
    TEST_START("ListNode LIFO order via add (add_after)");

    ListNode head;
    head.init();

    ListNode nodes[5];
    for (int i = 0; i < 5; i++) {
        head.add(nodes[i]);
    }

    int idx = 4;
    bool correct_order = true;
    for (auto* le = head.get_next(); le != &head; le = le->get_next()) {
        if (le != &nodes[idx]) {
            correct_order = false;
            break;
        }
        idx--;
    }

    TEST_ASSERT(correct_order && idx == -1, "add (add_after) produces LIFO traversal order");

    TEST_END();
}

// ============================================================================
// Multiple unlink and re-add
// ============================================================================

static void test_unlink_readd() {
    TEST_START("ListNode unlink then re-add");

    ListNode head;
    head.init();

    ListNode a, b;
    head.add(a);
    head.add(b);

    a.unlink();
    TEST_ASSERT(list_count(&head) == 1, "Count 1 after unlink");

    head.add_before(a);
    TEST_ASSERT(list_count(&head) == 2, "Count 2 after re-add");
    TEST_ASSERT(head.get_prev() == &a, "a is at tail after add_before");

    TEST_END();
}

// ============================================================================
// Test Runner
// ============================================================================

namespace list_test {

void test() {
    tests_passed = 0;
    tests_failed = 0;

    test_init_and_empty();
    test_add();
    test_add_before();
    test_unlink();
    test_fifo_order();
    test_lifo_order();
    test_unlink_readd();

    TEST_SUMMARY("Linked List");
}

}  // namespace list_test
