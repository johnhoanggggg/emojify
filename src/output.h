#pragma once

#include <memory>
#include <string>

#include "model.h"

// Saves the model as .png, .jpg or .svg (chosen by extension), with the
// longest side being size pixels.
void save(const Model& m, const std::string& path, int size);

// Streams an animated GIF of the model being built up.
class GifOutput {
public:
    GifOutput(const std::string& path, int w, int h);
    ~GifOutput();
    void add_frame(const Canvas& frame, int delay_centis);
    void close();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

std::string lower_ext(const std::string& path);
