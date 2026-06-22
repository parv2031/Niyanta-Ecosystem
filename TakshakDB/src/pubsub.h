#pragma once
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <mutex>

class PubSub {
private:
    std::unordered_map<std::string, std::unordered_set<int>> channels;
    std::unordered_map<int, std::unordered_set<std::string>> client_channels;
    std::mutex mtx;

public:
    void subscribe(int client_fd, const std::string& channel);
    void unsubscribe(int client_fd, const std::string& channel);
    void unsubscribe_all(int client_fd);
    int publish(const std::string& channel, const std::string& message);
};
