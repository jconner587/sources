#include "audio_io.h"
#include <portaudio.h>
#include <cstring>

#define SAMPLE_RATE 16000
#define FRAMES_PER_BUFFER 512
#define NUM_CHANNELS 1
#define SAMPLE_SIZE (sizeof(int16_t))

static PaStream* input_stream = nullptr;
static PaStream* output_stream = nullptr;

AudioIO::AudioIO() {
    Pa_Initialize();
    Pa_OpenDefaultStream(&input_stream, NUM_CHANNELS, 0, paInt16, SAMPLE_RATE, FRAMES_PER_BUFFER, nullptr, nullptr);
    Pa_OpenDefaultStream(&output_stream, 0, NUM_CHANNELS, paInt16, SAMPLE_RATE, FRAMES_PER_BUFFER, nullptr, nullptr);
    Pa_StartStream(input_stream);
    Pa_StartStream(output_stream);
}
AudioIO::~AudioIO() {
    Pa_StopStream(input_stream);
    Pa_CloseStream(input_stream);
    Pa_StopStream(output_stream);
    Pa_CloseStream(output_stream);
    Pa_Terminate();
}
AudioFrame AudioIO::capture() {
    AudioFrame frame;
    frame.data.resize(FRAMES_PER_BUFFER * SAMPLE_SIZE);
    Pa_ReadStream(input_stream, frame.data.data(), FRAMES_PER_BUFFER);
    frame.size = frame.data.size();
    return frame;
}
void AudioIO::play(const AudioFrame& frame) {
    Pa_WriteStream(output_stream, frame.data.data(), FRAMES_PER_BUFFER);
}