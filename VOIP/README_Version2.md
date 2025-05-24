# Robust VoIP FLTK Example

A cross-platform, robust C++ VoIP demo with FLTK GUI, PortAudio, and jitter buffer.

## Features

- FLTK GUI for call control
- TCP signaling, UDP/RTP audio
- Jitter buffer for smooth playback
- Modular for NAT traversal, echo cancellation, noise suppression

## Build

Requires: FLTK, PortAudio, CMake, pthread

```sh
sudo apt install libfltk1.3-dev portaudio19-dev cmake g++
mkdir build && cd build
cmake ..
make
```

## Usage

Run on two machines (or two terminals, using 127.0.0.1 for local test). Enter peer's IP, click Call or Answer.

## Extending

- Add echo cancellation/noise suppression with WebRTC, SpeexDSP, or RNNoise
- Add NAT traversal (ICE/STUN/TURN)
- Add audio level meters to GUI