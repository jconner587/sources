#pragma once
#include <vector>
#include <cstdint>

struct AudioFrame {
    std::vector<uint8_t> data;
    size_t size;
};

class AudioIO {
public:
    AudioIO();
    ~AudioIO();
    AudioFrame capture();
    void play(const AudioFrame& frame);
};