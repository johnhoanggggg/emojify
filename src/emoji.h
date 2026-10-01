#pragma once

#include <array>
#include <random>
#include <string>
#include <vector>

using Rng = std::mt19937_64;
using Color = std::array<float, 3>;

// One mipmap level of an emoji: premultiplied RGBA (colors 0..255, alpha
// 0..1), stored with a one texel transparent border so that bilinear sampling
// never needs bounds checks. Texel (x, y) lives at ((y + 1) * stride + x + 1) * 4.
struct Mip {
    int size = 0;
    int stride = 0;
    std::vector<float> pix;
};

struct Sprite {
    std::string name;
    std::string unified;
    std::string chars;  // UTF-8
    std::string category;
    std::vector<Mip> levels;  // levels[0] is full resolution, each next is half
    Color mean{};             // alpha-weighted mean color
    float coverage = 0;       // mean alpha
    std::vector<int> similar; // sprites with the closest mean colors
};

struct SpriteFilter {
    std::string only;        // if non-empty, only emoji appearing in this string
    std::string categories;  // comma separated, case-insensitive substrings
    bool skin_tones = false;
    bool flags = false;
};

// Returns the directory with the cached Apple emoji images, downloading them
// first if needed.
std::string emoji_dir(const std::string& override_dir);

std::vector<Sprite> load_sprites(const std::string& dir, const SpriteFilter& filter, int threads);

// Index of a random sprite among the n whose mean color is closest to c.
int nearest_sprite(const std::vector<Sprite>& sprites, const Color& c, int n, Rng& rng);

// Splits a UTF-8 string of emoji into unified codepoint sequences
// ("1F468-1F3FD-200D-1F4BB").
std::vector<std::string> split_emoji(const std::string& s);
std::string normalize_unified(const std::string& u);
std::string unified_to_utf8(const std::string& u);
