#include "jitter_buffer.h"
#include <cstring>

void JitterBuffer::push(const void* data, size_t size) {
    std::unique_lock<std::mutex> lock(mtx);
    if (buffer.size() >= MAX_SIZE) buffer.pop();
    AudioFrame f;
    f.data.resize(size);
    std::memcpy(f.data.data(), data, size);
    f.size = size;
    buffer.push(f);
    cv.notify_one();
}

AudioFrame JitterBuffer::pop() {
    std::unique_lock<std::mutex> lock(mtx);
    while (buffer.empty()) cv.wait(lock);
    AudioFrame f = buffer.front();
    buffer.pop();
    return f;
}