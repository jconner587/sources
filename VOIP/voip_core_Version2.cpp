#include "voip_core.h"
#include <iostream>
#include <cstring>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#define SIGNAL_PORT 6000
#define AUDIO_PORT  6002

VoipCore::VoipCore() : running(false) {}
VoipCore::~VoipCore() { hangup(); }

void VoipCore::start_call(const std::string& peer_ip) {
    running = true;
    signaling_thread = std::thread(&VoipCore::signaling_loop, this, peer_ip, true);
}

void VoipCore::answer_call() {
    running = true;
    signaling_thread = std::thread(&VoipCore::signaling_loop, this, "", false);
}

void VoipCore::hangup() {
    running = false;
    if (signaling_thread.joinable()) signaling_thread.join();
    if (audio_send_thread.joinable()) audio_send_thread.join();
    if (audio_recv_thread.joinable()) audio_recv_thread.join();
}

void VoipCore::signaling_loop(const std::string& peer_ip, bool is_caller) {
    // Simple TCP signaling: exchange "CALL"/"ANSWER" messages.
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(SIGNAL_PORT);

    if (is_caller) {
        inet_pton(AF_INET, peer_ip.c_str(), &addr.sin_addr);
        if (connect(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            std::cerr << "Signaling connect failed\n";
            return;
        }
        send(sockfd, "CALL", 4, 0);
        char buf[8] = {};
        recv(sockfd, buf, 8, 0); // Wait for "ANSWER"
    } else {
        addr.sin_addr.s_addr = INADDR_ANY;
        bind(sockfd, (struct sockaddr*)&addr, sizeof(addr));
        listen(sockfd, 1);
        int clientfd = accept(sockfd, nullptr, nullptr);
        char buf[8] = {};
        recv(clientfd, buf, 8, 0); // Wait for "CALL"
        send(clientfd, "ANSWER", 6, 0);
        close(sockfd);
        sockfd = clientfd;
    }
    close(sockfd);

    // Start audio threads
    audio_send_thread = std::thread(&VoipCore::audio_send_loop, this, peer_ip);
    audio_recv_thread = std::thread(&VoipCore::audio_recv_loop, this);
}

void VoipCore::audio_send_loop(const std::string& peer_ip) {
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in dest{};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(AUDIO_PORT);
    inet_pton(AF_INET, peer_ip.c_str(), &dest.sin_addr);

    while(running) {
        AudioFrame frame = audio.capture();
        sendto(sockfd, frame.data.data(), frame.size, 0, (struct sockaddr*)&dest, sizeof(dest));
    }
    close(sockfd);
}

void VoipCore::audio_recv_loop() {
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(AUDIO_PORT);
    bind(sockfd, (struct sockaddr*)&addr, sizeof(addr));
    char buf[2048];
    while(running) {
        ssize_t len = recv(sockfd, buf, sizeof(buf), 0);
        if (len > 0) {
            jitter.push(buf, len);
            AudioFrame f = jitter.pop();
            audio.play(f);
        }
    }
    close(sockfd);
}