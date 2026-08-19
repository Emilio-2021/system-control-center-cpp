#include "scc_bcrypt.h"

#include <cassert>

int main() {
    assert(bcrypt::validatePassword(
        "admin123",
        "$2b$12$/lVkRkHPnCbMUMQ9P9YmIeeUJNWtUaLQz22IqyzPnrr1bgQCuR5IK"));
    return 0;
}
