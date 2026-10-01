#pragma once

#include <cstdint>
#include <vector>

#include "emoji.h"

inline constexpr double kPi = 3.14159265358979323846;

// RGB float image, values 0..255.
struct Canvas {
    int w = 0, h = 0;
    std::vector<float> pix;

    Canvas() = default;
    Canvas(int w, int h, const Color& fill);
    Color average() const;
    std::vector<uint8_t> to_rgba() const;
};

// Loads an image (png, jpg, gif, bmp, ...), compositing transparency over
// white and area-averaging it down so the longest side is at most max_size.
Canvas load_image(const std::string& path, int max_size);

// One placed emoji: sprite index, center, side length and rotation in radians,
// all in canvas pixels.
struct Shape {
    int e = 0;
    double x = 0, y = 0, s = 0, a = 0;
};

class Model {
public:
    Model(Canvas target, const std::vector<Sprite>& sprites, Color bg, int workers, uint64_t seed);

    float opacity = 1;
    double min_size = 24;
    double max_size;
    double max_angle;

    // Finds and adds one emoji; false if no improving placement was found.
    bool step(int trials, int max_age);
    // Root mean squared error normalized to 0..1, like primitive.
    double score() const;
    Canvas render(double scale) const;

    const Canvas& target() const { return target_; }
    const std::vector<Shape>& shapes() const { return shapes_; }
    const std::vector<Sprite>& sprites() const { return sprites_; }
    const Color& background() const { return bg_; }

private:
    double delta(const Shape& sh) const;
    void add(const Shape& sh);
    Color box_mean(double x, double y, double s) const;
    Shape random_shape(Rng& rng) const;
    Shape mutate(Shape sh, Rng& rng) const;
    std::pair<Shape, double> search(Rng& rng, int n, int max_age) const;

    Canvas target_, current_;
    const std::vector<Sprite>& sprites_;
    Color bg_;
    std::vector<Shape> shapes_;
    double sse_ = 0;
    std::vector<double> sat_;  // summed area table of the target
    std::vector<Rng> rngs_;
};
