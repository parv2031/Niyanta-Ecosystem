#include <iostream>
#include <string>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <thread>
#include "parser.h"
#include "lru_cache.h"
#include "aof.h"
#include "pubsub.h"
#include <atomic>
#include <chrono>

std::atomic<int> active_connections{0};
auto start_time = std::chrono::steady_clock::now();

#define PORT 6379
#define BUFFER_SIZE 1048576

// Global Database State
LRUCache cache(1000000);

// Global Persistence Engine
AOF aof("data/takshak.aof");

// Global PubSub Engine
PubSub pubsub;

void handle_client(int client_fd) {
    active_connections++;
    std::vector<char> buffer(BUFFER_SIZE, 0);
    std::string client_buffer = "";
    bool is_subscribed = false;

    while (true) {
        ssize_t bytes_read = read(client_fd, buffer.data(), BUFFER_SIZE - 1);
        
        if (bytes_read < 0) {
            std::cerr << "Read error on client_fd " << client_fd << "\n";
            break;
        } else if (bytes_read == 0) {
            std::cout << "Client disconnected (client_fd " << client_fd << ").\n";
            break;
        }

        buffer[bytes_read] = '\0'; 
        client_buffer += buffer.data();

        // Process all complete commands in the buffer
        size_t pos;
        while ((pos = client_buffer.find('\n')) != std::string::npos) {
            std::string raw_input = client_buffer.substr(0, pos);
            client_buffer.erase(0, pos + 1);
            
            // Remove trailing \r if present
            if (!raw_input.empty() && raw_input.back() == '\r') {
                raw_input.pop_back();
            }

            if (raw_input.empty()) continue;

            Command cmd = Parser::parse(raw_input);
            std::string response;

            if (is_subscribed) {
                if (cmd.name == "SUBSCRIBE") {
                    if (cmd.args.size() == 1) {
                        pubsub.subscribe(client_fd, cmd.args[0]);
                        response = "*3\r\n$9\r\nsubscribe\r\n$" + std::to_string(cmd.args[0].length()) + "\r\n" + cmd.args[0] + "\r\n:1\r\n";
                    } else {
                        response = "-ERR wrong number of arguments for 'SUBSCRIBE' command\r\n";
                    }
                } else if (cmd.name == "UNSUBSCRIBE") {
                if (cmd.args.size() == 1) {
                    pubsub.unsubscribe(client_fd, cmd.args[0]);
                    response = "*3\r\n$11\r\nunsubscribe\r\n$" + std::to_string(cmd.args[0].length()) + "\r\n" + cmd.args[0] + "\r\n:1\r\n";
                } else {
                    response = "-ERR wrong number of arguments for 'UNSUBSCRIBE' command\r\n";
                }
            } else {
                response = "-ERR only (P)SUBSCRIBE / (P)UNSUBSCRIBE / PING / QUIT allowed in this context\r\n";
            }
        } else {
            if (cmd.name == "PING") {
                response = "+PONG\r\n";
            } 
        else if (cmd.name == "SET") {
            if (cmd.args.size() >= 2) {
                cache.put(cmd.args[0], cmd.args[1]);
                aof.append(cmd); // Phase 6: Log to disk
                response = "+OK\r\n";
            } else {
                response = "-ERR wrong number of arguments for 'SET' command\r\n";
            }
        } 
        else if (cmd.name == "GET") {
            if (cmd.args.size() == 1) {
                auto val = cache.get(cmd.args[0]);
                if (val.has_value()) {
                    response = "$" + std::to_string(val.value().length()) + "\r\n" + val.value() + "\r\n";
                } else {
                    response = "$-1\r\n"; 
                }
            } else {
                response = "-ERR wrong number of arguments for 'GET' command\r\n";
            }
        } 
        else if (cmd.name == "DEL") {
            if (cmd.args.size() == 1) {
                if (cache.remove_key(cmd.args[0])) {
                    aof.append(cmd); // Phase 6: Log to disk
                    response = ":1\r\n"; 
                } else {
                    response = ":0\r\n"; 
                }
            } else {
                response = "-ERR wrong number of arguments for 'DEL' command\r\n";
            }
        }
        else if (cmd.name == "EXPIRE") { 
            if (cmd.args.size() == 2) {
                try {
                    int seconds = std::stoi(cmd.args[1]);
                    if (cache.expire(cmd.args[0], seconds)) {
                        aof.append(cmd); // Phase 6: Log to disk
                        response = ":1\r\n"; 
                    } else {
                        response = ":0\r\n"; 
                    }
                } catch (...) {
                    response = "-ERR value is not an integer or out of range\r\n";
                }
            } else {
                response = "-ERR wrong number of arguments for 'EXPIRE' command\r\n";
            }
        }
        else if (cmd.name == "SUBSCRIBE") {
            if (cmd.args.size() == 1) {
                pubsub.subscribe(client_fd, cmd.args[0]);
                is_subscribed = true;
                response = "*3\r\n$9\r\nsubscribe\r\n$" + std::to_string(cmd.args[0].length()) + "\r\n" + cmd.args[0] + "\r\n:1\r\n";
            } else {
                response = "-ERR wrong number of arguments for 'SUBSCRIBE' command\r\n";
            }
        }
        else if (cmd.name == "PUBLISH") {
            if (cmd.args.size() == 2) {
                int count = pubsub.publish(cmd.args[0], cmd.args[1]);
                response = ":" + std::to_string(count) + "\r\n";
            } else {
                response = "-ERR wrong number of arguments for 'PUBLISH' command\r\n";
            }
        }
        else if (cmd.name == "INFO") {
            auto uptime = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start_time).count();
            std::string info_json = "{\"uptime_seconds\":" + std::to_string(uptime) +
                                    ",\"active_connections\":" + std::to_string(active_connections.load()) +
                                    ",\"total_keys\":" + std::to_string(cache.get_size()) +
                                    ",\"max_capacity\":" + std::to_string(cache.get_capacity()) +
                                    ",\"cache_hits\":" + std::to_string(cache.hits.load()) +
                                    ",\"cache_misses\":" + std::to_string(cache.misses.load()) +
                                    ",\"evictions\":" + std::to_string(cache.evictions.load()) + "}";
            response = "$" + std::to_string(info_json.length()) + "\r\n" + info_json + "\r\n";
        }
        else if (cmd.name == "") {
            continue;
        }
        else {
            response = "-ERR unknown command '" + cmd.name + "'\r\n";
        }
        } // End of else (not subscribed)

        send(client_fd, response.c_str(), response.length(), 0);
        } // End of inner while loop

    } // End of outer while(true) loop

    pubsub.unsubscribe_all(client_fd);
    active_connections--;
    close(client_fd);
}

int main() {
    // Phase 6: Restore database state from disk before accepting any connections
    aof.restore(cache);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::cerr << "Failed to create socket\n";
        return 1;
    }

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        std::cerr << "setsockopt failed\n";
        return 1;
    }

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY; 
    address.sin_port = htons(PORT);       

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        std::cerr << "Bind failed. Port might be in use.\n";
        return 1;
    }

    if (listen(server_fd, SOMAXCONN) < 0) {
        std::cerr << "Listen failed\n";
        return 1;
    }

    std::cout << "TakshakDB Server is listening on port " << PORT << "...\n";

    while (true) {
        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);
        if (client_fd < 0) {
            std::cerr << "Accept failed\n";
            continue;
        }
        
        std::cout << "New Client Connected! Spawning thread for client_fd " << client_fd << "...\n";
        
        std::thread t(handle_client, client_fd);
        t.detach(); 
    }

    close(server_fd);
    return 0;
}
