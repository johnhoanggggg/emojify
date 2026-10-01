// emojify reproduces images with Apple emoji, in the spirit of
// fogleman/primitive: emoji are added one at a time, each chosen by random
// search plus hill climbing to minimize the error against the target image.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "emoji.h"
#include "model.h"
#include "output.h"

namespace {

struct Config {
    std::string input;
    std::vector<std::string> outputs;
    int count = 600;
    int input_size = 320;
    int output_size = 2048;
    int alpha = 255;
    std::string bg;
    int workers = (int)std::max(1u, std::thread::hardware_concurrency());
    int trials = 400;
    int max_age = 100;
    double rot = 45;
    double min_size = 24;
    double max_size = 0;
    int nth = 1;
    SpriteFilter filter;
    std::string emoji_dir;
    uint64_t seed = 0;
    bool verbose = false;
};

void usage() {
    std::fprintf(stderr, R"(usage: emojify -i input.jpg -o output.png [flags]

  -i PATH        input image (png, jpg, gif, bmp, ...)
  -o PATH        output: .png, .jpg, .svg or .gif; may be repeated. A path with
                 %%d (e.g. frame%%03d.png) saves every -nth frame
  -n N           number of emoji (600)
  -r N           resize the input to this size before processing (320)
  -s N           output size (2048)
  -a N           emoji opacity, 1-255 (255)
  -bg HEX        background color (default: image average)
  -rot DEG       maximum rotation in degrees, 0 keeps emoji upright (45)
  -min PX        minimum emoji size, in pixels of the resized input (24)
  -max PX        maximum emoji size (default: a quarter of the resized input)
  -emojis STR    only use these emoji, e.g. "🍎🍊🍋🍏🫐🍇"
  -cat LIST      only use these categories, comma separated, e.g. "food,animals"
  -skin          include skin tone variations
  -flags         include country flags
  -t N           random candidates per emoji, split across workers (400)
  -age N         hill climbing stops after N failed mutations in a row (100)
  -j N           parallel workers (number of CPUs)
  -nth N         save every Nth frame for %%d outputs and .gif (1)
  -emoji-dir DIR cached emoji images (default: user cache directory)
  -seed N        random seed (default: time based)
  -v             print each emoji as it is placed
)");
}

Config parse_args(int argc, char** argv) {
    Config c;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a.rfind("--", 0) == 0) a = a.substr(1);
        std::string val;
        bool has_val = false;
        if (auto eq = a.find('='); eq != std::string::npos) {
            val = a.substr(eq + 1);
            a = a.substr(0, eq);
            has_val = true;
        }
        auto next = [&]() -> std::string {
            if (has_val) return val;
            if (i + 1 >= argc) throw std::runtime_error("missing value for " + a);
            return argv[++i];
        };
        auto num = [&]() { return std::stod(next()); };
        if (a == "-h" || a == "-help") { usage(); std::exit(0); }
        else if (a == "-i") c.input = next();
        else if (a == "-o") c.outputs.push_back(next());
        else if (a == "-n") c.count = (int)num();
        else if (a == "-r") c.input_size = (int)num();
        else if (a == "-s") c.output_size = (int)num();
        else if (a == "-a") c.alpha = (int)num();
        else if (a == "-bg") c.bg = next();
        else if (a == "-rot") c.rot = num();
        else if (a == "-min") c.min_size = num();
        else if (a == "-max") c.max_size = num();
        else if (a == "-emojis") c.filter.only = next();
        else if (a == "-cat") c.filter.categories = next();
        else if (a == "-skin") c.filter.skin_tones = true;
        else if (a == "-flags") c.filter.flags = true;
        else if (a == "-t") c.trials = (int)num();
        else if (a == "-age") c.max_age = (int)num();
        else if (a == "-j") c.workers = std::max(1, (int)num());
        else if (a == "-nth") c.nth = std::max(1, (int)num());
        else if (a == "-emoji-dir") c.emoji_dir = next();
        else if (a == "-seed") c.seed = std::stoull(next());
        else if (a == "-v") c.verbose = true;
        else throw std::runtime_error("unknown flag " + a + " (see -h)");
    }
    return c;
}

Color parse_hex(std::string s) {
    if (!s.empty() && s[0] == '#') s = s.substr(1);
    if (s.size() == 3) s = {s[0], s[0], s[1], s[1], s[2], s[2]};
    char* end = nullptr;
    unsigned long v = std::strtoul(s.c_str(), &end, 16);
    if (s.size() != 6 || *end) throw std::runtime_error("invalid color " + s);
    return {(float)((v >> 16) & 255), (float)((v >> 8) & 255), (float)(v & 255)};
}

double seconds_since(std::chrono::steady_clock::time_point t) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t).count();
}

std::string format_path(const std::string& pattern, int i) {
    char buf[4096];
    std::snprintf(buf, sizeof buf, pattern.c_str(), i);
    return buf;
}

int run(Config c) {
    Canvas target = load_image(c.input, c.input_size);
    std::string dir = emoji_dir(c.emoji_dir);
    auto start = std::chrono::steady_clock::now();
    std::vector<Sprite> sprites = load_sprites(dir, c.filter, c.workers);
    if (c.verbose) std::fprintf(stderr, "loaded %zu emoji in %.2fs\n", sprites.size(), seconds_since(start));

    Color bg = c.bg.empty() ? target.average() : parse_hex(c.bg);
    if (c.seed == 0) c.seed = (uint64_t)std::chrono::high_resolution_clock::now().time_since_epoch().count();
    Model m(std::move(target), sprites, bg, c.workers, c.seed);
    m.opacity = std::clamp(c.alpha, 1, 255) / 255.f;
    m.max_angle = c.rot * kPi / 180;
    m.min_size = std::max(1.0, c.min_size);
    if (c.max_size > 0) m.max_size = c.max_size;
    m.max_size = std::max(m.max_size, m.min_size);

    const double scale = (double)c.output_size / std::max(m.target().w, m.target().h);
    std::vector<std::unique_ptr<GifOutput>> gifs;
    for (const auto& o : c.outputs) {
        if (lower_ext(o) == ".gif") {
            gifs.push_back(std::make_unique<GifOutput>(o, (int)std::lround(m.target().w * scale),
                                                       (int)std::lround(m.target().h * scale)));
        }
    }

    start = std::chrono::steady_clock::now();
    int fails = 0;
    for (int i = 1; i <= c.count; i++) {
        if (!m.step(c.trials, c.max_age)) {
            // nothing improved this round; retry a few times before giving up
            if (++fails > 10) {
                std::fprintf(stderr, "stopping early at %zu emoji: no further improvement found\n", m.shapes().size());
                break;
            }
            i--;
            continue;
        }
        fails = 0;
        if (c.verbose) {
            const Sprite& sp = m.sprites()[m.shapes().back().e];
            std::fprintf(stderr, "%d: t=%.3f, score=%.6f, %s %s\n", i, seconds_since(start), m.score(),
                         sp.chars.c_str(), sp.name.c_str());
        }
        bool last = i == c.count;
        if (i % c.nth == 0 || last) {
            if (!gifs.empty()) {
                Canvas frame = m.render(scale);
                for (auto& g : gifs) g->add_frame(frame, last ? 300 : 4);
            }
            for (const auto& o : c.outputs) {
                if (o.find('%') != std::string::npos && lower_ext(o) != ".gif") save(m, format_path(o, i), c.output_size);
            }
        }
    }

    for (const auto& o : c.outputs) {
        if (lower_ext(o) != ".gif" && o.find('%') == std::string::npos) save(m, o, c.output_size);
    }
    for (auto& g : gifs) g->close();
    std::fprintf(stderr, "%zu emoji, score %.6f, %.1fs\n", m.shapes().size(), m.score(), seconds_since(start));
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        Config c = parse_args(argc, argv);
        if (c.input.empty() || c.outputs.empty()) {
            usage();
            return 2;
        }
        return run(std::move(c));
    } catch (const std::exception& e) {
        std::fprintf(stderr, "emojify: %s\n", e.what());
        return 1;
    }
}
