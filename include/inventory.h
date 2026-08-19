#pragma once

#include <cstddef>
#include <mutex>
#include <string>
#include <unordered_map>

struct Product {
    int id;
    std::string name;
    int stock_quantity;
};

class Inventory {
public:
    bool add_product(Product product);
    bool try_reserve(int product_id, int quantity);
    bool restock(int product_id, int quantity);
    std::size_t size() const;

private:
    mutable std::mutex mutex_;
    std::unordered_map<int, Product> products_;
};
