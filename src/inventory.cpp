#include "inventory.h"

bool Inventory::add_product(Product product) {
    if (product.id <= 0 || product.stock_quantity < 0 || product.name.empty()) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    return products_.emplace(product.id, std::move(product)).second;
}

bool Inventory::try_reserve(int product_id, int quantity) {
    if (quantity <= 0) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto product = products_.find(product_id);
    if (product == products_.end() || product->second.stock_quantity < quantity) {
        return false;
    }

    product->second.stock_quantity -= quantity;
    return true;
}

bool Inventory::restock(int product_id, int quantity) {
    if (quantity <= 0) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto product = products_.find(product_id);
    if (product == products_.end()) {
        return false;
    }

    product->second.stock_quantity += quantity;
    return true;
}

std::size_t Inventory::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return products_.size();
}
