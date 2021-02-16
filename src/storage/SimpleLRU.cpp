#include <iostream>
#include <limits>
#include <memory>

#include "SimpleLRU.h"

namespace Afina {
namespace Backend {

// See MapBasedGlobalLockImpl.h
bool SimpleLRU::Put(const std::string &key, const std::string &value) {
    // Check if pair size more than max value of type
    if (std::numeric_limits<size_t>::max() - key.size() < value.size()) { // maybe decltype?
        return false;
    }

    // Check if pair size more than max size of cache
    if (this->_max_size < value.size() + key.size()) {
        return false;
    }

    // Delete pairs from queue while we can't insert new pair.
    auto free_queue_space = [&](size_t need_space) {
        while (_cur_free < need_space) {
            if (!this->_lru_last) {
                break;
            }
            _cur_free += this->_lru_last->key.size() + this->_lru_last->value.size();

            // 1. Delete from map
            // I think it's more profitable to delete pair from map, even if needed key was already there.
            this->_lru_index.erase(std::cref(this->_lru_last->key));

            // 2. Delete from queue
            this->_lru_last = this->_lru_last->prev;
            if (this->_lru_last) {
                this->_lru_last->next = nullptr;
            }
        }
    };

    // Check if key already in lru_index
    // If it's inside, then move it to head, free space, change value.
    // Else, simply freeing space and insert pair in head.

    auto key_wrapper = std::cref(key);

    auto node_it = this->_lru_index.find(key);

    if (node_it != this->_lru_index.end()) {
        auto cur_node = node_it->second;
        std::unique_ptr<lru_node> cur_uniq;
        if (this->_lru_head->key != key) {
            cur_uniq = std::move(cur_node.get().prev->next);
            // Delete element from it's position
            if (cur_node.get().next) {
                cur_node.get().next->prev = cur_node.get().prev;
            }
            else {
                // If delete from end, then set new lru_last
                this->_lru_last = cur_node.get().prev;
            }
            cur_node.get().prev->next = std::move(cur_node.get().next);
            this->_lru_head->prev = cur_uniq.get();
            cur_uniq->next = std::move(this->_lru_head);
            cur_uniq->prev = nullptr;
            this->_lru_head = std::move(cur_uniq);
        }
        size_t need_space =
            (value.size() < this->_lru_head->value.size()) ? 0 : (value.size() - this->_lru_head->value.size());
        free_queue_space(need_space);
        cur_node.get().value = value;
        this->_cur_free -= need_space;
    } else {
        //        std::cerr << "KEY NOT IN MAP" << std::endl;
        size_t need_space = (key.size() + value.size());
        free_queue_space(need_space);
        auto new_node = std::unique_ptr<lru_node>(new lru_node(key, value));
        if (this->_lru_head) {
            this->_lru_head->prev = new_node.get();
            new_node->next = std::move(this->_lru_head);
        }
        else {
            this->_lru_last = new_node.get();
        }
        this->_lru_head = std::move(new_node);
        this->_lru_index.emplace(std::ref(_lru_head->key), std::reference_wrapper<lru_node>(*_lru_head));
        this->_cur_free -= need_space;
    }
    return true;
}

// See MapBasedGlobalLockImpl.h
bool SimpleLRU::PutIfAbsent(const std::string &key, const std::string &value) {
    auto key_wrapper = std::cref(key);
    if (this->_lru_index.find(key_wrapper) != this->_lru_index.end()) {
        return false;
    }
    return this->Put(key, value);
}

// See MapBasedGlobalLockImpl.h
bool SimpleLRU::Set(const std::string &key, const std::string &value) {
    auto key_wrapper = std::cref(key);
    if (this->_lru_index.find(key_wrapper) == this->_lru_index.end()) {
        return false;
    }
    return this->Put(key, value);
}

// See MapBasedGlobalLockImpl.h
bool SimpleLRU::Delete(const std::string &key) {
    auto key_wrapper = std::cref(key);

    auto node_it = this->_lru_index.find(key);
    if (node_it == this->_lru_index.end()) {
        return false;
    }
    auto cur_node = node_it->second;
    this->_lru_index.erase(key_wrapper);
    if (this->_lru_head->key == key) {
        this->_lru_head = std::move(this->_lru_head->next);
        this->_lru_head->prev = nullptr;
    } else {
        if (cur_node.get().next) {
            cur_node.get().next->prev = cur_node.get().prev;
        }
        cur_node.get().prev->next = std::move(cur_node.get().next);
    }
    return true;
}

// See MapBasedGlobalLockImpl.h
bool SimpleLRU::Get(const std::string &key, std::string &value) {
    auto key_wrapper = std::cref(key);
    auto node_it = this->_lru_index.find(key_wrapper);
    if (node_it == this->_lru_index.end()) {
        return false;
    }
    value = node_it->second.get().value;
    bool hitted = this->Put(key, value);
    return hitted;
}

} // namespace Backend
} // namespace Afina
