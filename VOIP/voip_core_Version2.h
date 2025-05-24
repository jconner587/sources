#pragma once
#include <string>
#include <thread>
#include <atomic>
#include "audio_io.h"
#include "jitter_buffer.h"

class VoipCore {
public:
    VoipCore();
    ~VoipCore();

    void start_call(const std::string& peer_ip);
    void answer_call();
    void hangup();

private:
    std::atomic<bool> running;
    std::thread signaling_thread;
    std::thread audio_send_thread;
    std::thread audio_recv_thread;

    AudioIO audio;
    JitterBuffer jitter;

    void signaling_loop(const std::string& peer_ip, bool is_caller);
    void audio_send_loop(const std::string& peer_ip);
    void audio_recv_loop();
};