#include "lru_cache.h"
#include <iostream>

LRUCache::LRUCache(int cap) : capacity(cap), stop_thread(false) {
    // Initialize dummy head and tail nodes
    head = new Node("", ""); 
    tail = new Node("", ""); 
    head->next = tail;
    tail->prev = head;

    // Phase 5: Start the background cleanup thread
    cleanup_thread = std::thread(&LRUCache::background_cleanup_task, this);
}

LRUCache::~LRUCache() {
    // Phase 5: Signal the background thread to stop, and wait for it to finish
    stop_thread = true;
    if (cleanup_thread.joinable()) {
        cleanup_thread.join();
    }

    Node* curr = head;
    while (curr != nullptr) {
        Node* next = curr->next;
        delete curr;
        curr = next;
    }
}

// Helper: Remove an existing node from the linked list
void LRUCache::remove(Node* node) {
    Node* prev_node = node->prev;
    Node* next_node = node->next;
    prev_node->next = next_node;
    next_node->prev = prev_node;
}

// Helper: Insert a node right after the dummy head
void LRUCache::insert_head(Node* node) {
    Node* next_node = head->next;
    head->next = node;
    node->prev = head;
    node->next = next_node;
    next_node->prev = node;
}

std::optional<std::string> LRUCache::get(const std::string& key) {
    std::lock_guard<std::mutex> lock(cache_mutex);
    
    if (cache.find(key) != cache.end()) {
        Node* node = cache[key];

        // Phase 5 (Passive Expiry): Check if the key has expired right before we return it
        if (node->has_expiry && std::chrono::steady_clock::now() > node->expiry_time) {
            remove(node);
            cache.erase(key);
            delete node;
            misses++;
            return std::nullopt; // Act as if the key doesn't exist
        }

        // It was accessed, so move it to the head
        remove(node);
        insert_head(node);
        hits++;
        return node->value;
    }
    misses++;
    return std::nullopt;
}

void LRUCache::put(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(cache_mutex);

    if (cache.find(key) != cache.end()) {
        // Key exists: update value and move to head
        Node* node = cache[key];
        node->value = value;
        
        // Redis standard behavior: Overwriting a key strips its TTL
        node->has_expiry = false; 
        
        remove(node);
        insert_head(node);
    } else {
        // Key doesn't exist: insert it
        if (cache.size() == capacity) {
            // Memory is full, evict the LRU item
            Node* lru_node = tail->prev;
            cache.erase(lru_node->key);
            remove(lru_node);
            delete lru_node;
            evictions++;
        }
        Node* new_node = new Node(key, value);
        cache[key] = new_node;
        insert_head(new_node);
    }
}

bool LRUCache::remove_key(const std::string& key) {
    std::lock_guard<std::mutex> lock(cache_mutex);

    if (cache.find(key) != cache.end()) {
        Node* node = cache[key];
        remove(node);
        cache.erase(key);
        delete node;
        return true;
    }
    return false;
}

// Phase 5: Attach an expiration timestamp to an existing key
bool LRUCache::expire(const std::string& key, int seconds) {
    std::lock_guard<std::mutex> lock(cache_mutex);

    if (cache.find(key) != cache.end()) {
        Node* node = cache[key];
        node->has_expiry = true;
        // Calculate the exact future time when this key should die
        node->expiry_time = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
        return true;
    }
    return false;
}

// Phase 5: Background daemon that constantly sweeps for dead keys (Active Expiry)
void LRUCache::background_cleanup_task() {
    while (!stop_thread) {
        // Run the sweep every 1 second
        std::this_thread::sleep_for(std::chrono::seconds(1));

        // We must lock the mutex before sweeping because we might modify the Map/List
        std::lock_guard<std::mutex> lock(cache_mutex);
        
        auto it = cache.begin();
        while (it != cache.end()) {
            Node* node = it->second;
            
            // If the node has a TTL and the current time is past its expiry time...
            if (node->has_expiry && std::chrono::steady_clock::now() > node->expiry_time) {
                // Destroy it!
                remove(node);
                delete node;
                it = cache.erase(it); // erase returns the iterator to the *next* element
            } else {
                ++it;
            }
        }
    }
}

int LRUCache::get_size() {
    std::lock_guard<std::mutex> lock(cache_mutex);
    return cache.size();
}

int LRUCache::get_capacity() {
    return capacity;
}
