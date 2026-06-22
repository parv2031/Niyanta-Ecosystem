#include "pubsub.h"
#include <sys/socket.h>
#include <iostream>

void PubSub::subscribe(int client_fd, const std::string& channel) {
    std::lock_guard<std::mutex> lock(mtx);
    channels[channel].insert(client_fd);
    client_channels[client_fd].insert(channel);
}

void PubSub::unsubscribe(int client_fd, const std::string& channel) {
    std::lock_guard<std::mutex> lock(mtx);
    if (channels.count(channel)) {
        channels[channel].erase(client_fd);
        if (channels[channel].empty()) {
            channels.erase(channel);
        }
    }
    if (client_channels.count(client_fd)) {
        client_channels[client_fd].erase(channel);
    }
}

void PubSub::unsubscribe_all(int client_fd) {
    std::lock_guard<std::mutex> lock(mtx);
    if (client_channels.count(client_fd)) {
        for (const auto& channel : client_channels[client_fd]) {
            channels[channel].erase(client_fd);
            if (channels[channel].empty()) {
                channels.erase(channel);
            }
        }
        client_channels.erase(client_fd);
    }
}

int PubSub::publish(const std::string& channel, const std::string& message) {
    std::lock_guard<std::mutex> lock(mtx);
    if (!channels.count(channel)) return 0;

    int count = 0;
    // Format response as RESP Array: *3\r\n$7\r\nmessage\r\n$<channel_len>\r\n<channel>\r\n$<msg_len>\r\n<msg>\r\n
    std::string response = "*3\r\n$7\r\nmessage\r\n$" + std::to_string(channel.length()) + "\r\n" + channel + "\r\n$" + std::to_string(message.length()) + "\r\n" + message + "\r\n";

    for (int client_fd : channels[channel]) {
        if (send(client_fd, response.c_str(), response.length(), MSG_NOSIGNAL) >= 0) {
            count++;
        }
    }
    return count;
}
