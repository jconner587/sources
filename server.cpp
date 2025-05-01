#include <iostream>
#include <string>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <thread>
#include <mutex>
#include <vector>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>

// Constants
constexpr size_t BUFFER_SIZE = 4096;

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

// Function to handle file transfer
void handle_file_transfer(int client_fd) {
    char buffer[BUFFER_SIZE];

    std::cout << "Step 1: Receiving filename...\n";
    memset(buffer, 0, sizeof(buffer));
    ssize_t bytes_received = reliable_recv(client_fd, buffer, sizeof(buffer));
    std::cout << "Bytes received for filename: " << bytes_received << "\n";
    if (bytes_received <= 0) {
        std::cerr << "Failed to receive filename.\n";
        reliable_send(client_fd, "ERROR", 5);
        return;
    }
    std::string filename(buffer);
    std::cout << "Filename received: " << filename << "\n";

    reliable_send(client_fd, "ACK", 3);
    std::cout << "Step 2: Acknowledged filename.\n";

    // Continue with file size and data handling...
}

// Function to handle client connections
void handle_client(int client_fd) {
    char buffer[BUFFER_SIZE];

    while (true) {
        memset(buffer, 0, sizeof(buffer));
        ssize_t bytes_received = recv(client_fd, buffer, sizeof(buffer), 0);

        if (bytes_received > 0) {
            std::string message(buffer);

            if (message.find("CMD:SENDFILE") != std::string::npos) {
                handle_file_transfer(client_fd);
            } else if (message == "/quit\n") {
                std::cout << "Client disconnected.\n";
                break;
            } else {
                std::cout << "Client: " << message;
            }

        } else if (bytes_received == 0) {
            std::cout << "Client disconnected.\n";
            break;
        } else {
            std::cerr << "Connection error.\n";
            break;
        }
    }

    close(client_fd);
}

// Main function
int main() {
    int port;
    std::cout << "Enter server port: ";
    std::cin >> port;

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::cerr << "Failed to create socket.\n";
        return 1;
    }

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(server_fd, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "Failed to bind socket.\n";
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, 5) < 0) {
        std::cerr << "Failed to listen on socket.\n";
        close(server_fd);
        return 1;
    }

    std::cout << "Server is listening on port " << port << "...\n";

    while (true) {
        int client_fd = accept(server_fd, nullptr, nullptr);
        if (client_fd < 0) {
            std::cerr << "Failed to accept connection.\n";
            continue;
        }

        std::cout << "New client connected.\n";
        std::thread(handle_client, client_fd).detach();
    }

    close(server_fd);
    return 0;
}