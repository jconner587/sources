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

// Function to handle incoming file transfer from client
void handle_file_transfer(int client_fd) {
    char buffer[BUFFER_SIZE];

    // Step 1: Receive the filename
    memset(buffer, 0, sizeof(buffer));
    ssize_t bytes_received = reliable_recv(client_fd, buffer, sizeof(buffer));
    if (bytes_received <= 0) {
        std::cerr << "Failed to receive filename.\n";
        reliable_send(client_fd, "ERROR", 5);
        return;
    }
    std::string filename(buffer);

    // Step 2: Acknowledge the filename
    reliable_send(client_fd, "ACK", 3);

    // Step 3: Receive the file size
    memset(buffer, 0, sizeof(buffer));
    bytes_received = reliable_recv(client_fd, buffer, sizeof(buffer));
    if (bytes_received <= 0) {
        std::cerr << "Failed to receive file size.\n";
        reliable_send(client_fd, "ERROR", 5);
        return;
    }
    size_t file_size = std::stoul(buffer);

    // Step 4: Acknowledge the file size
    reliable_send(client_fd, "ACK", 3);

    // Step 5: Receive the file data
    std::ofstream output_file("Downloads/" + filename, std::ios::binary);
    size_t total_received = 0;
    while (total_received < file_size) {
        bytes_received = reliable_recv(client_fd, buffer, sizeof(buffer));
        if (bytes_received <= 0) {
            std::cerr << "Failed to receive file data.\n";
            reliable_send(client_fd, "ERROR", 5);
            break;
        }
        output_file.write(buffer, bytes_received);
        total_received += bytes_received;
    }
    output_file.close();

    // Step 6: Acknowledge transfer completion
    if (total_received == file_size) {
        reliable_send(client_fd, "TRANSFER_COMPLETE", 17);
        std::cout << "File transfer complete: " << filename << "\n";
    } else {
        reliable_send(client_fd, "ERROR", 5);
        std::cerr << "File transfer incomplete.\n";
    }
}

// Function to handle client commands and file transfers
void client_handler(int client_fd) {
    char buffer[BUFFER_SIZE];
    while (server_running) {
        ssize_t bytes_received = reliable_recv(client_fd, buffer, BUFFER_SIZE);
        if (bytes_received <= 0) {
            break; // Disconnect client
        }

        // Handle file transfer commands
        std::string command(buffer, bytes_received);
        if (command.find("Ready to receive") != std::string::npos) {
            handle_file_transfer(client_fd);
        } else {
            // Handle other commands
        }
    }
    close(client_fd);
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
            std::lock_guard<std::mutex> lock(clients_mutex);
            if (clients.count(client_fd) > 0) {
                close(client_fd);
                std::cout << YELLOW << "Client with FD " << client_fd << " has been kicked." << RESET << "\n";
                clients.erase(client_fd);
                client_ips.erase(client_fd);
            } else {
                std::cout << RED << "No client with FD " << client_fd << " found." << RESET << "\n";
            }
        } else if (command == "help") {
            std::cout << GREEN << R"(
Available Commands:
  help                 - Show this menu
  list                 - List connected users
  kick <FD>            - Disconnect a user by file descriptor
  shutdown / exit      - Stop the server
)" << RESET;
        } else {
            std::cout << RED << "Unknown command. Type 'help'." << RESET << "\n";
        }
    }
}

// Main server function
int main() {
    signal(SIGINT, signal_handler);

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

    std::thread(admin_cli).detach(); // Start the admin CLI in a separate thread

    while (server_running) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (sockaddr*)&client_addr, &client_len);

        if (client_fd != -1) {
            std::lock_guard<std::mutex> lock(clients_mutex);
            clients[client_fd] = "Anonymous";
            client_ips[client_fd] = inet_ntoa(client_addr.sin_addr);

            std::thread(client_handler, client_fd).detach();
        }
    }

    close(server_fd);
    return 0;
}