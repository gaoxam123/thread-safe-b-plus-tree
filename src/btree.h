#pragma once
#include <optional>
#include <shared_mutex>
#include <algorithm>

template<typename KeyT, typename ValueT, typename ComparatorT, std::size_t kCapacity>
struct Btree {
    struct Node {
        // Level in the tree
        uint16_t level;
        // Number of children
        uint16_t key_count;
        // Lock for each node
        mutable std::shared_mutex mtx;

        // Constructor
        Node(uint16_t level, uint16_t key_count) : level(level), key_count(key_count) {}

        // Manual locking
        void lock_read() const    { mtx.lock_shared(); }
        void unlock_read() const  { mtx.unlock_shared(); }
        void lock_write()         { mtx.lock(); }
        void unlock_write()       { mtx.unlock(); }

        // Check if the node is a leaf
        bool is_leaf() const {
            return level == 0;
        }
    };

    struct InnerNode: Node {
        // Keys
        KeyT keys[kCapacity];
        // Children
        Node* children[kCapacity];

        // Constructor
        InnerNode() : Node(1, 0) {}

        // Get the index of the first key that is not less than than a provided key
        std::pair<uint32_t, bool> lower_bound(const KeyT &key) {
            if (this->key_count == 0) {
                return {0, false};
            }

            const ComparatorT comparator{};

            int left = 0, right = this->key_count - 2;
            int index_found = -1;
            while (left <= right) {
                int mid = (left + right) >> 1;
                if (comparator(keys[mid], key)) {
                    left = mid + 1;
                } 
                else {
                    index_found = mid;
                    right = mid - 1;
                }
            }

            if (index_found == -1) {
                return {this->key_count - 1, false};
            }
            return {index_found, true};
        }

        // Insert a key
        void insert_split(const KeyT &key, Node* split_child) {
            uint32_t index = lower_bound(key).first;

            for (size_t i = this->key_count - 1; i > index; i--) {
                keys[i] = keys[i - 1];
                children[i + 1] = children[i];
            }
            keys[index] = key;
            children[index + 1] = split_child;
            this->key_count++;
        }

        // Split a node
        KeyT split(InnerNode* right_neighbor) {
            int mid_key_index = (this->key_count - 1) / 2;
            
            int left_count = mid_key_index + 1;
            int right_count = this->key_count - left_count;

            this->key_count = left_count;
            right_neighbor->key_count = right_count;

            std::copy(keys + mid_key_index + 1, keys + mid_key_index + 1 + right_count, right_neighbor->keys);
            std::copy(children + mid_key_index + 1, children + mid_key_index + 1 + right_count, right_neighbor->children);

            return keys[mid_key_index];
        }
    };

    struct LeafNode: Node {
        // Keys
        KeyT keys[kCapacity];
        // Values
        ValueT values[kCapacity];

        // Constructor
        LeafNode() : Node(0, 0) {}

        // Get the index of the first key that is not less than than a provided key
        std::pair<uint32_t, bool> lower_bound(const KeyT &key) {
            if (this->key_count == 0) {
                return {0, false};
            }

            int left = 0, right = this->key_count - 1;
            int index_found = this->key_count;

            const ComparatorT comparator{};

            while (left <= right) {
                int mid = (left + right) >> 1;
                if (comparator(keys[mid], key)) {
                    left = mid + 1;
                } 
                else {
                    index_found = mid;
                    right = mid - 1;
                }
            }

            bool found = false;
            if (index_found < static_cast<int>(this->key_count)) {
                found = !comparator(keys[index_found], key) &&
                        !comparator(key, keys[index_found]);
            }
            return {static_cast<uint32_t>(index_found), found};
        }

        // Insert a key
        void insert(const KeyT &key, const ValueT &value) {
            auto [index, found] = lower_bound(key);
            if (found) {
                values[index] = value;
                return;
            }

            for (uint32_t i = this->key_count; i > index; i--) {
                keys[i] = keys[i - 1];
                values[i] = values[i - 1];
            }
            keys[index] = key;
            values[index] = value;
            this->key_count++;
        }

        // Split a node
        KeyT split(LeafNode* right_neighbor) {
            int mid_key_index = this->key_count / 2;

            int left_count = mid_key_index + 1;
            int right_count = this->key_count - left_count;

            this->key_count = left_count;
            right_neighbor->key_count = right_count;

            std::copy(keys + mid_key_index + 1, keys + mid_key_index + 1 + right_count, right_neighbor->keys);
            std::copy(values + mid_key_index + 1, values + mid_key_index + 1 + right_count, right_neighbor->values);

            return keys[mid_key_index];
        }
    };

    // The root
    Node* root;
    // Global lock for the tree
    mutable std::shared_mutex global_mutex;

    // Constructor
    Btree() {
        root = nullptr;
    }

    // Destructor
    ~Btree() = default;

    // Lookup an entry in the tree
    std::optional<ValueT> get(const KeyT &key) {
        if (!root) {
            return std::nullopt;
        }

        root->lock_read();
        Node* current_node = root;

        // Lock coupling until reaching a leaf
        while (!current_node->is_leaf()) {
            InnerNode* current_inner_node = static_cast<InnerNode*>(current_node);
            uint32_t pos = current_inner_node->lower_bound(key).first;
            Node* child_node = current_inner_node->children[pos];

            child_node->lock_read();
            current_node->unlock_read();

            current_node = child_node;
        }

        LeafNode* leafNode = static_cast<LeafNode*>(current_node);
        auto [pos, found] = leafNode->lower_bound(key);
        if (!found) {
            leafNode->unlock_read();
            return std::nullopt;
        }

        ValueT res = leafNode->values[pos];
        current_node->unlock_read();

        return res;
    }

    // Insert a new entry into the tree
    void put(const KeyT &key, const ValueT &value) {
        // Global lock for cases where the root is updated
        global_mutex.lock();

        // Empty tree
        if (!root) {
            auto* leaf = new LeafNode();
            root = leaf;
            leaf->insert(key, value);
            global_mutex.unlock();

            return;
        }
        
        const ComparatorT comparator{};
        root->lock_write();
        Node* current_node = root;

        if (current_node->is_leaf()) {
            LeafNode* leafNode = static_cast<LeafNode*>(current_node);
            // Need to split the node
            if (kCapacity <= leafNode->key_count) {
                LeafNode* right_neighbor_node;
                InnerNode* new_root;
                right_neighbor_node = new LeafNode();
                new_root = new InnerNode();

                right_neighbor_node->lock_write();
                
                KeyT separator_key = leafNode->split(right_neighbor_node);
                
                new_root->lock_write();

                // Create a new root
                InnerNode* parent_node = new_root;

                parent_node->level = 1;
                parent_node->key_count = 1;
                parent_node->children[0] = root;
                parent_node->insert_split(separator_key, right_neighbor_node);
                parent_node->unlock_write();

                root = new_root;
                global_mutex.unlock();

                if (comparator(separator_key, key)) {
                    leafNode = right_neighbor_node;
                    current_node->unlock_write();
                    current_node = right_neighbor_node;
                } 
                else {
                    right_neighbor_node->unlock_write();
                }

                leafNode->insert(key, value);
                current_node->unlock_write();
            }
            else {
                global_mutex.unlock();
                leafNode->insert(key, value);
                current_node->unlock_write();
            }
            return;
        }
        else {
            InnerNode* innerNode = static_cast<InnerNode*>(current_node);
            // Need to split the node
            if (kCapacity <= innerNode->key_count) {
                InnerNode* right_neighbor_node;
                InnerNode* new_root;
                right_neighbor_node = new InnerNode();
                new_root = new InnerNode();

                right_neighbor_node->lock_write();
                KeyT separator_key = innerNode->split(right_neighbor_node);
                right_neighbor_node->level = innerNode->level;

                if (comparator(separator_key, key)) {
                    current_node->unlock_write();
                    current_node = right_neighbor_node;
                } 
                else {
                    right_neighbor_node->unlock_write();
                }

                new_root->lock_write();

                // Create a new root
                InnerNode* parent_node = new_root;
                parent_node->level = innerNode->level + 1;
                parent_node->key_count = 1;
                parent_node->children[0] = root;
                root = new_root;
                parent_node->insert_split(separator_key, right_neighbor_node);
                parent_node->unlock_write();
            }
            global_mutex.unlock();
            // Lock coupling
            while (true) {
                innerNode = static_cast<InnerNode*>(current_node);
                uint32_t pos = innerNode->lower_bound(key).first;
                Node* child_node = innerNode->children[pos];
                child_node->lock_write();

                if (innerNode->level == 1) {
                    LeafNode* child_node_leaf = static_cast<LeafNode*>(child_node);
                    // Need to split the node
                    if (kCapacity <= child_node_leaf->key_count) {
                        LeafNode* right_neighbor_node;
                        right_neighbor_node = new LeafNode();
                        right_neighbor_node->lock_write();
                        KeyT separator_key = child_node_leaf->split(right_neighbor_node);

                        if (comparator(separator_key, key)) {
                            child_node_leaf->unlock_write();
                            child_node_leaf = right_neighbor_node;
                        } 
                        else {
                            right_neighbor_node->unlock_write();
                        }
                        innerNode->insert_split(separator_key, right_neighbor_node);
                    }

                    child_node_leaf->insert(key, value);
                    current_node->unlock_write();
                    child_node_leaf->unlock_write();

                    return;
                }
                
                InnerNode* child_node_inner = static_cast<InnerNode*>(child_node);
                // Need to split the node
                if (kCapacity <= child_node_inner->key_count) {
                    InnerNode* right_neighbor_node;
                    right_neighbor_node = new InnerNode();
                    right_neighbor_node->lock_write();
                    KeyT separator_key = child_node_inner->split(right_neighbor_node);
                    right_neighbor_node->level = child_node_inner->level;

                    if (comparator(separator_key, key)) {
                        child_node_inner->unlock_write();
                        child_node_inner = static_cast<InnerNode*>(right_neighbor_node);
                    } 
                    else {
                        right_neighbor_node->unlock_write();
                    }
                    innerNode->insert_split(separator_key, right_neighbor_node);
                }
                current_node->unlock_write();
                current_node = child_node_inner;
            }
        }
    }
};
