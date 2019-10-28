#include "SimpleLRU.h"

namespace Afina {
namespace Backend {

// See MapBasedGlobalLockImpl.h
bool SimpleLRU::Put(const std::string &key, const std::string &value) {
    auto it = _lru_index.find(key);
    if (it != _lru_index.end()) {
        return setValue(it, value);
    } else {
        return insertNewNode(key, value);
    }
}

// See MapBasedGlobalLockImpl.h
bool SimpleLRU::PutIfAbsent(const std::string &key, const std::string &value) {

    auto it = _lru_index.find(key);
    if (it != _lru_index.end()) {
        return false;
    } else {
        return insertNewNode(key, value);
    }
}

// See MapBasedGlobalLockImpl.h
bool SimpleLRU::Set(const std::string &key, const std::string &value) {

    auto it = _lru_index.find(key);
    if (it != _lru_index.end()) {
        return setValue(it, value);
    } else {
        return false;
    }
}

// See MapBasedGlobalLockImpl.h
bool SimpleLRU::Delete(const std::string &key) {

    auto it = _lru_index.find(key);
    if (it == _lru_index.end()) {
        return false;
    }

    auto node = it->second;
    _current_size -= (node.get().key.size() + node.get().value.size());
    _lru_index.erase(it);

    if (&node.get() == _lru_head.get()) {
        std::unique_ptr<lru_node> tmp(std::move(_lru_head));
        _lru_head = std::move(tmp->next);
    } else if (&node.get() == _lru_tail) {
        _lru_tail = _lru_tail->prev;
        _lru_tail->next.reset();
    } else {
        node.get().next->prev = node.get().prev;
        node.get().prev->next.swap(node.get().next);
        node.get().next.reset();
    }

    return true;

}

// See MapBasedGlobalLockImpl.h
bool SimpleLRU::Get(const std::string &key, std::string &value) {

    auto it = _lru_index.find(key);
    if (it == _lru_index.end()) {
        return false;
    }

    value = it->second.get().value;
    updateList(it->second);
    return true;

}

void SimpleLRU::updateList(std::reference_wrapper<lru_node> current) {

    lru_node& node = current.get();
    if (_lru_tail == &node) {
        return;
    }

    if (_lru_head.get() != &node) {
        node.next->prev = node.prev;
        node.prev->next.swap(node.next);
    } else {
        _lru_head.swap(node.next);
        _lru_head->prev = nullptr;
    }

    _lru_tail->next.swap(node.next);
    node.prev = _lru_tail;
    _lru_tail = &node;

}

bool SimpleLRU::setValue(lru_index::iterator it, const std::string &value) {

    auto node = it->second;
    if (node.get().key.size() + value.size() > _max_size) {
        return false;
    }

    if (value.size() > node.get().value.size()) {
        deleteOldest(value.size() - node.get().value.size());
    } else {
        _current_size -= node.get().value.size();
    }

    node.get().value = value;
    _current_size += value.size();
    
    updateList(node);
    return true;
}

void SimpleLRU::deleteOldest(size_t extra_size) {

    while (_current_size + extra_size > _max_size) {
        _lru_index.erase(_lru_head->key);

        if (_lru_head->next != nullptr) {
            _lru_head->next->prev = _lru_head->prev;

            std::unique_ptr<lru_node> tmp(std::move(_lru_head));
            _lru_head = std::move(tmp->next);
        } else {
            _lru_tail = nullptr;
            _lru_head.reset();
        }
        _current_size -= _lru_head->key.size() + _lru_head->value.size();
    }
}

bool SimpleLRU::insertNewNode(const std::string &key, const std::string &value) {

    if (key.size() + value.size() > _max_size) {
        return false;
    }

    deleteOldest(key.size() + value.size());

    std::unique_ptr<lru_node> new_node (new lru_node{key, value, nullptr, nullptr});
    if (_lru_tail != nullptr) {
        new_node->prev = _lru_tail;
        _lru_tail->next.swap(new_node);
        _lru_tail = _lru_tail->next.get();
    } else {
        _lru_head.swap(new_node);
        _lru_tail = _lru_head.get();
    }

    _lru_index.insert(std::make_pair(std::ref(_lru_tail->key), std::ref(*_lru_tail)));
    _current_size += key.size() + value.size();
    return true;

}

} // namespace Backend
} // namespace Afina
