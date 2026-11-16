#include "test/test_defs.h"
#include <debug/assert.h>
#include <sys/intrusive_list.hpp>

static int tests_passed = 0;
static int tests_failed = 0;
struct ListItem {
    sys::list_node<ListItem> link{*this};
};
using ItemList = sys::intrusive_list<ListItem, &ListItem::link>;

static int list_count(const ItemList& list) {
    int count = 0;
    for ([[maybe_unused]] const auto& item : list) {
        ++count;
    }
    return count;
}
static void test_init_and_empty() {
    TEST_START("Intrusive list init and empty");
    ItemList list;
    ListItem item;
    TEST_ASSERT(list.empty() && list.begin() == list.end(), "Empty list has no objects");
    TEST_ASSERT(!item.link.is_linked(), "New hook is detached");
    TEST_ASSERT(list_count(list) == 0, "Empty list count is zero");
    TEST_END();
}
static void test_add() {
    TEST_START("Intrusive list push_front");
    ItemList list;
    ListItem a, b, c;
    list.push_front(a);
    TEST_ASSERT(&list.front() == &a && &list.back() == &a, "First object is both front and back");
    list.push_front(b);
    list.push_front(c);
    TEST_ASSERT(&list.front() == &c && &list.back() == &a, "Push front keeps original tail");
    TEST_ASSERT(list_count(list) == 3, "All three objects are linked");
    list.clear();
    TEST_END();
}
static void test_add_before() {
    TEST_START("Intrusive list insertion");
    ItemList list;
    ListItem a, b, c;
    list.push_back(a);
    list.push_back(c);
    list.insert(list.iterator_to(c), b);
    auto it = list.begin();
    TEST_ASSERT(&*it++ == &a && &*it++ == &b && &*it++ == &c && it == list.end(),
                "Insert places object before the supplied position");
    list.clear();
    TEST_END();
}
static void test_unlink() {
    TEST_START("Intrusive list erase and repeated unlink");
    ItemList list;
    ListItem a, b, c;
    list.push_back(a);
    list.push_back(b);
    list.push_back(c);
    auto next = list.erase(list.iterator_to(b));
    TEST_ASSERT(&*next == &c && !b.link.is_linked(), "Erase detaches and returns next object");
    b.link.unlink();
    TEST_ASSERT(list_count(list) == 2, "Repeated unlink preserves neighbours");
    for (auto it = list.begin(); it != list.end();) {
        it = list.erase(it);
    }
    TEST_ASSERT(list.empty() && !a.link.is_linked() && !c.link.is_linked(), "Erase while traversing empties list");
    TEST_END();
}
static void test_fifo_order() {
    TEST_START("Intrusive list FIFO and const traversal");
    ItemList list;
    ListItem items[5];
    for (auto& item : items) {
        list.push_back(item);
    }
    int index = 0;
    bool ordered = true;
    const auto& borrowed = list;
    for (const auto& item : borrowed) {
        ordered = ordered && &item == &items[index++];
    }
    TEST_ASSERT(ordered && index == 5, "Const traversal preserves FIFO order");
    list.clear();
    TEST_END();
}
static void test_lifo_order() {
    TEST_START("Intrusive list LIFO and reverse traversal");
    ItemList list;
    ListItem items[5];
    for (auto& item : items) {
        list.push_front(item);
    }
    int index = 0;
    bool ordered = true;
    for (auto& item : list.reversed()) {
        ordered = ordered && &item == &items[index++];
    }
    TEST_ASSERT(ordered && index == 5, "Reverse traversal reverses LIFO order");
    list.clear();
    TEST_END();
}
static void test_unlink_readd() {
    TEST_START("Intrusive list unlink and reinsert");
    ItemList list;
    ListItem a, b;
    list.push_front(a);
    list.push_front(b);
    a.link.unlink();
    list.push_back(a);
    TEST_ASSERT(&list.front() == &b && &list.back() == &a && list_count(list) == 2,
                "Detached hook can be inserted again");
    list.clear();
    TEST_END();
}
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
