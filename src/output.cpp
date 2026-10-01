#include "output.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>

#include "gif.h"
#include "stb_image_write.h"

std::string lower_ext(const std::string& path) {
    std::string ext = std::filesystem::path(path).extension().string();
    for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
    return ext;
}

static std::string xml_escape(const std::string& s) {
    std::string out;
    for (char c : s) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        default: out += c;
        }
    }
    return out;
}

// Each emoji becomes a text glyph, so the SVG renders with the viewer's emoji
// font (Apple Color Emoji on Apple devices).
static void save_svg(const Model& m, const std::string& path, double scale) {
    std::FILE* f = std::fopen(path.c_str(), "w");
    if (!f) throw std::runtime_error("cannot write " + path);
    double W = m.target().w * scale, H = m.target().h * scale;
    auto bg = m.background();
    auto c8 = [](float v) { return (int)std::clamp(std::lround(v), 0L, 255L); };
    std::fprintf(f, "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%.0f\" height=\"%.0f\" viewBox=\"0 0 %.0f %.0f\">\n",
                 W, H, W, H);
    std::fprintf(f, "<rect width=\"100%%\" height=\"100%%\" fill=\"rgb(%d,%d,%d)\"/>\n", c8(bg[0]), c8(bg[1]), c8(bg[2]));
    std::fprintf(f, "<g font-family=\"'Apple Color Emoji','Segoe UI Emoji','Noto Color Emoji',sans-serif\" "
                    "text-anchor=\"middle\" dominant-baseline=\"central\"");
    if (m.opacity < 1) std::fprintf(f, " fill-opacity=\"%.3f\"", m.opacity);
    std::fprintf(f, ">\n");
    for (const auto& sh : m.shapes()) {
        double x = sh.x * scale, y = sh.y * scale, s = sh.s * scale;
        // the glyph roughly fills its em box; shrink a little to match the sprite footprint
        std::fprintf(f, "<text x=\"%.1f\" y=\"%.1f\" font-size=\"%.1f\"", x, y, s * 0.82);
        if (sh.a != 0) std::fprintf(f, " transform=\"rotate(%.1f %.1f %.1f)\"", sh.a * 180 / kPi, x, y);
        std::fprintf(f, ">%s</text>\n", xml_escape(m.sprites()[sh.e].chars).c_str());
    }
    std::fprintf(f, "</g>\n</svg>\n");
    if (std::fclose(f) != 0) throw std::runtime_error("cannot write " + path);
}

void save(const Model& m, const std::string& path, int size) {
    double scale = (double)size / std::max(m.target().w, m.target().h);
    std::string ext = lower_ext(path);
    if (ext == ".svg") return save_svg(m, path, scale);
    Canvas c = m.render(scale);
    auto rgba = c.to_rgba();
    int ok;
    if (ext == ".jpg" || ext == ".jpeg") {
        ok = stbi_write_jpg(path.c_str(), c.w, c.h, 4, rgba.data(), 95);
    } else {
        ok = stbi_write_png(path.c_str(), c.w, c.h, 4, rgba.data(), c.w * 4);
    }
    if (!ok) throw std::runtime_error("cannot write " + path);
}

struct GifOutput::Impl {
    GifWriter writer{};
    bool open = false;
};

GifOutput::GifOutput(const std::string& path, int w, int h) : impl_(std::make_unique<Impl>()) {
    impl_->open = GifBegin(&impl_->writer, path.c_str(), (uint32_t)w, (uint32_t)h, 4);
    if (!impl_->open) throw std::runtime_error("cannot write " + path);
}

GifOutput::~GifOutput() {
    if (impl_ && impl_->open) GifEnd(&impl_->writer);
}

void GifOutput::add_frame(const Canvas& frame, int delay_centis) {
    auto rgba = frame.to_rgba();
    GifWriteFrame(&impl_->writer, rgba.data(), (uint32_t)frame.w, (uint32_t)frame.h, (uint32_t)delay_centis);
}

void GifOutput::close() {
    if (impl_->open) GifEnd(&impl_->writer);
    impl_->open = false;
}
