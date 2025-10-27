#include <iostream>
#include <cassert>
#include <thread>
#include <vector>
#include <random>
#include <chrono>
#include "btree.h"

// Define the type for keys and values
struct byte_array {
    const unsigned char* data;
    std::size_t size;
};

// Comparator used in the tree
struct less_bytes {
    bool operator()(const byte_array& a, const byte_array& b) const {
        std::size_t n = (a.size < b.size) ? a.size : b.size;

        for (std::size_t i = 0; i < n; ++i) {
            if (a.data[i] < b.data[i]) return true;
            if (a.data[i] > b.data[i]) return false;
        }
        return a.size < b.size;
    }
}; 

// Helper functions
static std::vector<unsigned char> encode_u64_be(uint64_t x) {
    std::vector<unsigned char> v(8);
    for (int i = 7; i >= 0; i--) {
        v[7 - i] = static_cast<unsigned char>((x >> (i * 8)) & 0xFF);
    }
    return v;
}
static byte_array make_ba(std::vector<unsigned char>& v) {
    return byte_array{ v.data(), v.size() };
}
static byte_array make_const_ba(const std::vector<unsigned char>& v) {
    return byte_array{ const_cast<unsigned char*>(v.data()), v.size() };
}
static bool bytes_equal(const byte_array& a, const byte_array& b) {
    if (a.size != b.size) return false;
    for (size_t i = 0; i < a.size; i++) {
        if (a.data[i] != b.data[i]) {
            return false;
        }
    }
    return true;
}

// Define a tiny ASSERT_TRUE
#define ASSERT_TRUE(expr)                                                     \
    do {                                                                      \
        if (!(expr)) {                                                        \
            std::cerr << "ASSERT_TRUE failed at " << __FILE__ << ":"          \
                      << __LINE__ << " -> " #expr << std::endl;               \
            std::abort();                                                     \
        }                                                                     \
    } while (0)

int main() {
    using Tree = Btree<byte_array, byte_array, less_bytes, 64>;
    constexpr size_t LeafCap = 64;
    constexpr size_t kThreads = 8;

    Tree tree;

    const size_t per_thread = 2 * LeafCap;
    const size_t total = kThreads * per_thread;

    // Backing storage so byte_array pointers remain valid for the lifetime of the test
    std::vector<std::vector<unsigned char>> key_store(total);
    std::vector<std::vector<unsigned char>> val_store(total);

    std::vector<std::thread> threads;

    // Run a simple multithreading test

    using clock = std::chrono::high_resolution_clock;

    auto start = clock::now();

    for (size_t t = 0; t < kThreads; ++t) {
        threads.emplace_back([&, t] {
            size_t startValue = t * per_thread;
            size_t limit = startValue + per_thread;

            // Insert values
            for (size_t i = startValue; i < limit; ++i) {
                key_store[i] = encode_u64_be(i);
                val_store[i] = encode_u64_be(2 * i);
                tree.insert(make_ba(key_store[i]), make_ba(val_store[i]));
            }

            // Read them back
            for (size_t i = startValue; i < limit; ++i) {
                auto res = tree.get(make_const_ba(key_store[i]));
                ASSERT_TRUE(res.has_value());
                ASSERT_TRUE(bytes_equal(*res, make_const_ba(val_store[i])));
            }
        });
    }

    for (auto& th : threads) th.join();

    auto end = clock::now();
    std::chrono::duration<double> elapsed = end - start;

    std::cout << "Elapsed time: " << elapsed.count() << " seconds\n";
    std::cout << "MultithreadWriters test passed.\n";
}
