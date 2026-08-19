#include "inventory.h"

#include <cassert>

int main() {
    Inventory inventory;
    assert(inventory.add_product({1, "Test Product", 5}));
    assert(!inventory.add_product({1, "Duplicate", 5}));
    assert(inventory.try_reserve(1, 5));
    assert(!inventory.try_reserve(1, 1));
    assert(inventory.restock(1, 2));
    assert(inventory.try_reserve(1, 2));
    assert(!inventory.restock(99, 1));
    return 0;
}
