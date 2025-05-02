#include <iostream>
#include <fstream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

#define BUFFER_SIZE 1024

// Function to send a file over a socket
void sendFile(int clientSocket, const std::string& filePath) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file) {
        std::cerr << "Error: Unable to open file " << filePath << std::endl;
        std::string errorMsg = "ERROR: File not found or cannot be opened.\n";
        send(clientSocket, errorMsg.c_str(), errorMsg.size(), 0);
        return;
    }

    std::cout << "Sending file: " << filePath << "\n";
    char buffer[BUFFER_SIZE];
    while (!file.eof()) {
        file.read(buffer, BUFFER_SIZE);
        size_t bytesRead = file.gcount();
        send(clientSocket, buffer, bytesRead, 0);
    }

    file.close();
    std::cout << "File sent successfully.\n";
}

// Function to handle client requests
void handleClientRequest(int clientSocket, const std::string& directory) {
    char buffer[BUFFER_SIZE];
    memset(buffer, 0, BUFFER_SIZE);

    // Receive the requested file name from the client
    int bytesReceived = recv(clientSocket, buffer, BUFFER_SIZE, 0);
    if (bytesReceived <= 0) {
        std::cerr << "Error: Failed to receive the file request from the client.\n";
        return;
    }

    std::string requestedFile(buffer);
    std::string filePath = directory + "/" + requestedFile;

    // Check if the file exists
    if (!fs::exists(filePath) || !fs::is_regular_file(filePath)) {
        std::string errorMsg = "ERROR: File not found.\n";
        send(clientSocket, errorMsg.c_str(), errorMsg.size(), 0);
        std::cerr << "Client requested a non-existent file: " << requestedFile << "\n";
        return;
    }

    // Send the requested file
    sendFile(clientSocket, filePath);
}

int main() {
    int port;
    std::string directory;

    // Get user input for the port and file directory
    std::cout << "Enter port number to listen on: ";
    std::cin >> port;
    std::cin.ignore(); // Ignore the newline character left in the buffer

    std::cout << "Enter the directory path: ";
    std::getline(std::cin, directory);

    if (!fs::is_directory(directory)) {
        std::cerr << "Error: The specified path is not a directory.\n";
        return 1;
    }

    int serverSocket, clientSocket;
    struct sockaddr_in serverAddr, clientAddr;
    socklen_t addrLen = sizeof(clientAddr);

    // Create socket
    serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket == -1) {
        std::cerr << "Error: Cannot create socket.\n";
        return 1;
    }

    // Bind socket to IP/Port
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(port);

    if (bind(serverSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        std::cerr << "Error: Bind failed.\n";
        return 1;
    }

    // Listen for connections
    if (listen(serverSocket, 1) < 0) {
        std::cerr << "Error: Listen failed.\n";
        return 1;
    }

    std::cout << "Server listening on port " << port << "...\n";

    // Accept connection
    clientSocket = accept(serverSocket, (struct sockaddr*)&clientAddr, &addrLen);
    if (clientSocket < 0) {
        std::cerr << "Error: Connection failed.\n";
        return 1;
    }

    std::cout << "Client connected. Waiting for file request...\n";
    handleClientRequest(clientSocket, directory);

    close(clientSocket);
    close(serverSocket);
    return 0;
}