# CameraW-mlv-Decoder
Decoder for .mlv files produced by [CameraW app](https://github.com/Amachua-Appure/CameraW) on android


## Compilation

The decoder is written in modern C++ and requires a compiler supporting the C++17 standard.

Compile the source code using GCC/G++:

```bash
g++ -O3 -std=c++17 decoder.cpp -o decoder.exe
```
*Note: For macOS/Linux environments, output as decoder instead of decoder.exe.*


# Usage

Run the compiled executable via the command line, passing the target MLV file as the single argument.

```bash

./decoder.exe input_video.mlv
```
----
# Expected Output

Upon successful execution, the decoder will output:
    audio.wav (if an audio stream was muxed during capture).
    A sequential list of DNG files (e.g., frame_0000.dng, frame_0001.dng) ready for import into DaVinci Resolve, Adobe Premiere, or other RAW video editors.
    
----

***
