#include <iostream>
#include <thread>
#include <string>
#include <cstring>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <mutex>
#include <optional>
#include <atomic>

// Constants
constexpr size_t BUFFER_SIZE = 4096;

// Global Variables
std::atomic<bool> running(true);
std::mutex pending_mutex;
std::optional<std::filesystem::path> pending_upload_path;

// Function to send a file to the server
void send_file_to_server(int sockfd, const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cerr << "Failed to open file: " << filepath << "\n";
        return;
    }

    std::streamsize file_size = file.tellg();
    file.seekg(0, std::ios::beg);

    // Step 1: Send the filename
    std::string filename = std::filesystem::path(filepath).filename().string();
    if (send(sockfd, filename.c_str(), filename.size(), 0) <= 0) {
        std::cerr << "Failed to send filename.\n";
        return;
    }

    // Step 2: Send FILESIZE
    std::string size_message = std::to_string(file_size);
    if (send(sockfd, size_message.c_str(), size_message.size(), 0) <= 0) {
        std::cerr << "Failed to send file size.\n";
        return;
    }

    // Step 3: Wait for acknowledgment from the server
    char ack_buffer[64];
    memset(ack_buffer, 0, sizeof(ack_buffer));
    ssize_t ack_received = recv(sockfd, ack_buffer, sizeof(ack_buffer), 0);
    if (ack_received <= 0 || std::string(ack_buffer).find("ACK") == std::string::npos) {
        std::cerr << "Failed to receive valid acknowledgment from server.\n";
        return;
    }

    std::cout << "Server acknowledged. Starting file transfer...\n";

    // Step 4: Send file data
    char buffer[BUFFER_SIZE];
    size_t total_sent = 0;
    while (file) {
        file.read(buffer, sizeof(buffer));
        std::streamsize bytes_read = file.gcount();
        if (bytes_read > 0) {
            if (send(sockfd, buffer, bytes_read, 0) < 0) {
                std::cerr << "Failed to send file chunk.\n";
                break;
            }
            total_sent += bytes_read;
        }
    }

    // Step 5: Wait for final acknowledgment
    memset(ack_buffer, 0, sizeof(ack_buffer));
    ack_received = recv(sockfd, ack_buffer, sizeof(ack_buffer), 0);
    if (ack_received > 0 && std::string(ack_buffer).find("TRANSFER_COMPLETE") != std::string::npos) {
        std::cout << "Server confirmed file transfer completion.\n";
    } else {
        std::cerr << "Failed to receive final acknowledgment from server.\n";
    }

    file.close();
    std::cout << "File upload complete.\n";
}

// Function to receive messages from the server
void receive_messages(int sockfd) {
    char buffer[BUFFER_SIZE];
    std::string partial_message;

    while (running) {
        memset(buffer, 0, sizeof(buffer));
        int bytes_received = recv(sockfd, buffer, sizeof(buffer), 0);

        if (bytes_received > 0) {
            partial_message.append(buffer, bytes_received);

            while (true) {
                size_t newline_pos = partial_message.find('\n');
                if (newline_pos == std::string::npos) break;

                std::string line = partial_message.substr(0, newline_pos + 1);
                partial_message.erase(0, newline_pos + 1);

                if (line.find("Ready to receive ") != std::string::npos) {
                    std::filesystem::path file_to_send;
                    {
                        std::lock_guard<std::mutex> lock(pending_mutex);
                        if (pending_upload_path) {
                            file_to_send = *pending_upload_path;
                            pending_upload_path.reset();
                        } else {
                            std::cerr << "No file queued for upload despite acknowledgment.\n";
                            continue;
                        }
                    }

                    send_file_to_server(sockfd, file_to_send.string());
                } else {
                    std::cout << line;
                }

                std::cout.flush();
            }
        } else if (bytes_received == 0) {
            std::cout << "Server closed the connection.\n";
            running = false;
            break;
        } else {
            std::cerr << "Connection error.\n";
            running = false;
            break;
        }
    }
}

// Function to show available commands
void show_help() {
    std::cout << "\nCommands:\n"
              << "/quit                 - Disconnect\n"
              << "/sendfile <filepath>  - Upload a file to the server\n"
              << "/listfiles            - List available files on the server\n"
              << "/help                 - Show this help menu\n\n";
}

// Main function
int main() {
    std::string server_ip;
    int port;

    std::cout << "Enter server IP (e.g., 127.0.0.1): ";
    std::getline(std::cin, server_ip);

    std::cout << "Enter server port: ";
    std::cin >> port;
    std::cin.ignore();

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        std::cerr << "Failed to create socket.\n";
        return 1;
    }

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, server_ip.c_str(), &server_addr.sin_addr) <= 0) {
        std::cerr << "Invalid IP address.\n";
        close(sockfd);
        return 1;
    }

    std::cout << "Connecting to server...\n";
    if (connect(sockfd, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "Connection failed.\n";
        close(sockfd);
        return 1;
    }

    std::thread receiver(receive_messages, sockfd);

    std::string input;
    while (running) {
        std::getline(std::cin, input);
        if (input == "/quit") {
            send(sockfd, "[Client disconnected]\n", 23, 0);
            running = false;
            break;
        } else if (input == "/help") {
            show_help();
        } else if (input.rfind("/sendfile ", 0) == 0) {
            std::filesystem::path fullpath = input.substr(10);
            if (!std::filesystem::exists(fullpath)) {
                std::cerr << "File not found: " << fullpath << "\n";
            } else {
                std::lock_guard<std::mutex> lock(pending_mutex);
                pending_upload_path = fullpath;
                std::string filename_only = fullpath.filename().string();
                std::string command = "Ready to receive " + filename_only + "\n";
                send(sockfd, command.c_str(), command.size(), 0);
            }
        } else if (input == "/listfiles") {
            send(sockfd, "/listfiles\n", 11, 0);
        } else if (!input.empty()) {
            input += "\n";
            send(sockfd, input.c_str(), input.size(), 0);
        }
    }

    shutdown(sockfd, SHUT_RDWR);
    close(sockfd);
    if (receiver.joinable()) receiver.join();

    std::cout << "Connection closed.\n";
    return 0;
}