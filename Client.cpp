#include <iostream>
#include <fstream>
#include <cstring>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

#define BUFFER_SIZE 1024

// Function to receive a file from the server
void receiveFile(int serverSocket, const std::string& outputFileName) {
    std::ofstream file(outputFileName, std::ios::binary);
    if (!file) {
        std::cerr << "Error: Unable to create file " << outputFileName << std::endl;
        return;
    }

    char buffer[BUFFER_SIZE];
    int bytesReceived;
    while ((bytesReceived = recv(serverSocket, buffer, BUFFER_SIZE, 0)) > 0) {
        file.write(buffer, bytesReceived);
    }

    file.close();
    std::cout << "File received successfully.\n";
}

int main() {
    std::string serverIP, outputFileName, requestedFile;
    int port;

    // Get user input for server details
    std::cout << "Enter server IP address: ";
    std::cin >> serverIP;
    std::cout << "Enter server port: ";
    std::cin >> port;
    std::cin.ignore(); // Ignore the newline character left in the buffer

    // Get the file name to request from the server
    std::cout << "Enter the name of the file you want to download: ";
    std::getline(std::cin, requestedFile);

    // Get the name to save the received file as
    std::cout << "Enter the name to save the received file as: ";
    std::getline(std::cin, outputFileName);

    int clientSocket;
    struct sockaddr_in serverAddr;

    // Create socket
    clientSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (clientSocket < 0) {
        std::cerr << "Error: Cannot create socket.\n";
        return 1;
    }

    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);

    // Convert IP address from text to binary
    if (inet_pton(AF_INET, serverIP.c_str(), &serverAddr.sin_addr) <= 0) {
        std::cerr << "Error: Invalid address.\n";
        return 1;
    }

    // Connect to server
    if (connect(clientSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        std::cerr << "Error: Connection failed.\n";
        return 1;
    }

    // Send the requested file name to the server
    send(clientSocket, requestedFile.c_str(), requestedFile.size(), 0);

    // Receive the file
    std::cout << "Downloading file...\n";
    receiveFile(clientSocket, outputFileName);

    close(clientSocket);
    return 0;
}