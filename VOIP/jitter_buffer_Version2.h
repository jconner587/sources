#pragma once
#include "audio_io.h"
#include <queue>
#include <mutex>
#include <condition_variable>

class JitterBuffer {
public:
    void push(const void* data, size_t size);
    AudioFrame pop();
private:
    std::queue<AudioFrame> buffer;
    std::mutex mtx;
    std::condition_variable cv;
    static const size_t MAX_SIZE = 50;
};