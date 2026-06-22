#pragma once
#include <string>
#include <unordered_map>
#include <optional>
#include <mutex>
#include <chrono>
#include <thread>
#include <atomic>

// Doubly Linked List Node
struct Node {
    std::string key;
    std::string value;
    Node* prev;
    Node* next;
    
    // Phase 5: TTL (Time-To-Live) fields
    bool has_expiry;
    std::chrono::time_point<std::chrono::steady_clock> expiry_time;

    Node(std::string k, std::string v) : key(k), value(v), prev(nullptr), next(nullptr), has_expiry(false) {}
};

class LRUCache {
private:
    int capacity;
    std::unordered_map<std::string, Node*> cache;
    
    Node* head; // Most recently used side
    Node* tail; // Least recently used side

    // Protects the cache from race conditions in a multi-threaded environment
    std::mutex cache_mutex;

    // Phase 5: Background active cleanup thread
    std::thread cleanup_thread;
    std::atomic<bool> stop_thread;
    void background_cleanup_task();

    void remove(Node* node);
    void insert_head(Node* node);

public:
    LRUCache(int cap);
    ~LRUCache();
    
    std::optional<std::string> get(const std::string& key);
    void put(const std::string& key, const std::string& value);
    bool remove_key(const std::string& key);
    
    // Phase 5: Set a key to expire in X seconds
    bool expire(const std::string& key, int seconds);

    // Phase 7: Telemetry/Metrics
    std::atomic<int> hits{0};
    std::atomic<int> misses{0};
    std::atomic<int> evictions{0};
    
    int get_size();
    int get_capacity();
};
