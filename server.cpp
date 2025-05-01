#include <iostream>
#include <thread>
#include <map>
#include <mutex>
#include <string>
#include <unistd.h>
#include <cstring>
#include <sqlite3.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fstream>
#include <ctime>
#include <sys/stat.h>
#include <algorithm>
#include <sstream>
#include <filesystem>
#include <chrono>
#include <iomanip>
#include <csignal>

#define RESET   "\033[0m"
#define RED     "\033[31m"
#define GREEN   "\033[32m"
#define YELLOW  "\033[33m"
#define CYAN    "\033[36m"
#define MAGENTA "\033[35m"

#define BUFFER_SIZE 4096

std::map<int, std::string> clients;
std::map<int, std::string> client_ips;
std::mutex clients_mutex;
bool server_running = true;
sqlite3* db = nullptr;

// Forward declarations for functions
void kick_client(int client_fd);
void ban_ip(const std::string& ip, const std::string& reason, int duration_minutes);
void signal_handler(int signal);

// Reliable send and receive functions
ssize_t reliable_send(int fd, const char* buffer, size_t length) {
    size_t total_sent = 0;
    while (total_sent < length) {
        ssize_t sent = send(fd, buffer + total_sent, length - total_sent, 0);
        if (sent <= 0) return sent; // Error or connection closed
        total_sent += sent;
    }
    return total_sent;
}

ssize_t reliable_recv(int fd, char* buffer, size_t length) {
    size_t total_received = 0;
    while (total_received < length) {
        ssize_t received = recv(fd, buffer + total_received, length - total_received, 0);
        if (received <= 0) return received; // Error or connection closed
        total_received += received;
    }
    return total_received;
}

// Logging function with formatted timestamps
void log_to_file(const std::string& entry) {
    std::ofstream log_file("server_log.txt", std::ios::app);
    if (log_file) {
        auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        log_file << "[" << std::put_time(std::localtime(&now), "%Y-%m-%d %H:%M:%S") << "] " << entry << "\n";
    }
}

// Ensure the Downloads directory exists
void ensure_downloads_directory() {
    std::filesystem::path downloads_dir("Downloads");
    if (!std::filesystem::exists(downloads_dir)) {
        std::filesystem::create_directory(downloads_dir);
        std::cout << "Created Downloads directory.\n";
    }
}

// Kick a client
void kick_client(int client_fd) {
    std::lock_guard<std::mutex> lock(clients_mutex);
    if (clients.count(client_fd) > 0) {
        close(client_fd);
        std::cout << YELLOW << "Client with FD " << client_fd << " has been kicked." << RESET << "\n";
        log_to_file("Client with FD " + std::to_string(client_fd) + " was kicked.");
        clients.erase(client_fd);
        client_ips.erase(client_fd);
    } else {
        std::cout << RED << "No client with FD " << client_fd << " found." << RESET << "\n";
    }
}

// Ban an IP address
void ban_ip(const std::string& ip, const std::string& reason, int duration_minutes) {
    const char* insert_sql = R"(
        INSERT OR REPLACE INTO banned_ips (ip_address, reason, ban_time, duration_minutes)
        VALUES (?, ?, ?, ?);
    )";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, insert_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << RED << "SQL error: " << sqlite3_errmsg(db) << RESET << "\n";
        return;
    }

    sqlite3_bind_text(stmt, 1, ip.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, reason.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 3, static_cast<int>(time(nullptr)));
    sqlite3_bind_int(stmt, 4, duration_minutes);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        std::cerr << RED << "SQL execution error: " << sqlite3_errmsg(db) << RESET << "\n";
    } else {
        std::cout << GREEN << "IP " << ip << " has been banned for " << duration_minutes << " minutes." << RESET << "\n";
        log_to_file("IP " + ip + " banned for " + std::to_string(duration_minutes) + " minutes. Reason: " + reason);
    }

    sqlite3_finalize(stmt);
}

// Signal handler for SIGINT
void signal_handler(int signal) {
    if (signal == SIGINT) {
        server_running = false;
        std::cout << YELLOW << "\nSIGINT received. Shutting down server..." << RESET << "\n";
    }
}

// Admin CLI for managing the server
void admin_cli() {
    std::string command;
    while (server_running) {
        std::cout << MAGENTA << "Admin> " << RESET;
        std::getline(std::cin, command);

        if (command == "shutdown" || command == "exit") {
            server_running = false;
            std::cout << YELLOW << "Server shutting down..." << RESET << "\n";
            break;
        } else if (command == "list") {
            std::lock_guard<std::mutex> lock(clients_mutex);
            std::cout << CYAN << "Connected clients:\n";
            for (const auto& [fd, username] : clients) {
                std::cout << "  FD " << fd << ": " << username << " (IP: " << client_ips[fd] << ")\n";
            }
            std::cout << RESET;
        } else if (command.starts_with("kick ")) {
            int client_fd = std::stoi(command.substr(5));
            kick_client(client_fd);
        } else if (command.starts_with("ban_ip ")) {
            std::istringstream iss(command.substr(7));
            std::string ip, reason;
            int duration;
            iss >> ip >> duration;
            std::getline(iss, reason);
            reason = reason.empty() ? "No reason provided" : reason.substr(1); // Trim leading space
            ban_ip(ip, reason, duration);
        } else if (command == "help") {
            std::cout << GREEN << R"(
Available Commands:
  help                 - Show this menu
  list                 - List connected users
  kick <FD>            - Disconnect a user by file descriptor
  ban_ip <IP> <minutes> <reason> - Ban an IP address
  shutdown / exit      - Stop the server
)" << RESET;
        } else {
            std::cout << RED << "Unknown command. Type 'help'." << RESET << "\n";
        }
    }
}

// Cleanup resources before exiting
void cleanup_resources(int server_fd) {
    close(server_fd);
    {
        std::lock_guard<std::mutex> lock(clients_mutex);
        for (const auto& [fd, _] : clients) {
            close(fd);
        }
        clients.clear();
    }
    if (db) sqlite3_close(db);
    std::cout << YELLOW << "Server resources cleaned up." << RESET << "\n";
}

// Main server function
int main() {
    signal(SIGINT, signal_handler);

    ensure_downloads_directory();

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        std::cerr << RED << "Socket creation failed" << RESET << "\n";
        return 1;
    }

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(6112);

    if (bind(server_fd, (sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        std::cerr << RED << "Socket bind failed" << RESET << "\n";
        return 1;
    }

    if (listen(server_fd, 10) == -1) {
        std::cerr << RED << "Socket listen failed" << RESET << "\n";
        return 1;
    }

    std::cout << GREEN << "Server running on port 6112..." << RESET << "\n";

    std::thread(admin_cli).detach();

    while (server_running) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (sockaddr*)&client_addr, &client_len);

        if (client_fd != -1) {
            std::lock_guard<std::mutex> lock(clients_mutex);
            clients[client_fd] = "Anonymous";
            client_ips[client_fd] = inet_ntoa(client_addr.sin_addr);

            std::thread([client_fd]() {
                char buffer[BUFFER_SIZE];
                while (server_running) {
                    ssize_t bytes_received = reliable_recv(client_fd, buffer, BUFFER_SIZE);
                    if (bytes_received <= 0) {
                        break; // Disconnect client
                    }
                    // Handle client commands or data transfer here
                }
                close(client_fd);
            }).detach();
        }
    }

    cleanup_resources(server_fd);
    return 0;
}