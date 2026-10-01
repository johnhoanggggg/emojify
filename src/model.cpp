#include "model.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <thread>

#include "stb_image.h"

Canvas::Canvas(int w, int h, const Color& fill) : w(w), h(h), pix((size_t)w * h * 3) {
    for (size_t i = 0; i < pix.size(); i += 3) {
        pix[i] = fill[0];
        pix[i + 1] = fill[1];
        pix[i + 2] = fill[2];
    }
}

Color Canvas::average() const {
    double s[3] = {0, 0, 0};
    for (size_t i = 0; i < pix.size(); i += 3) {
        s[0] += pix[i];
        s[1] += pix[i + 1];
        s[2] += pix[i + 2];
    }
    double n = (double)w * h;
    return {(float)(s[0] / n), (float)(s[1] / n), (float)(s[2] / n)};
}

static uint8_t clamp8(float v) {
    return v <= 0 ? 0 : v >= 255 ? 255 : (uint8_t)(v + 0.5f);
}

std::vector<uint8_t> Canvas::to_rgba() const {
    std::vector<uint8_t> out((size_t)w * h * 4);
    for (size_t i = 0; i < (size_t)w * h; i++) {
        out[i * 4] = clamp8(pix[i * 3]);
        out[i * 4 + 1] = clamp8(pix[i * 3 + 1]);
        out[i * 4 + 2] = clamp8(pix[i * 3 + 2]);
        out[i * 4 + 3] = 255;
    }
    return out;
}

Canvas load_image(const std::string& path, int max_size) {
    int sw, sh, n;
    unsigned char* data = stbi_load(path.c_str(), &sw, &sh, &n, 4);
    if (!data) throw std::runtime_error("cannot decode " + path + ": " + stbi_failure_reason());
    int w = sw, h = sh;
    if (max_size > 0 && (sw > max_size || sh > max_size)) {
        if (sw >= sh) {
            w = max_size;
            h = std::max(1, (int)std::lround((double)sh * max_size / sw));
        } else {
            h = max_size;
            w = std::max(1, (int)std::lround((double)sw * max_size / sh));
        }
    }
    Canvas c(w, h, {0, 0, 0});
    std::vector<float> cnt((size_t)w * h, 0.f);
    for (int y = 0; y < sh; y++) {
        int ty = (int)((long long)y * h / sh);
        for (int x = 0; x < sw; x++) {
            int tx = (int)((long long)x * w / sw);
            const unsigned char* p = data + ((size_t)y * sw + x) * 4;
            float a = p[3] / 255.f, white = 255.f * (1 - a);
            size_t i = (size_t)ty * w + tx;
            c.pix[i * 3] += p[0] * a + white;
            c.pix[i * 3 + 1] += p[1] * a + white;
            c.pix[i * 3 + 2] += p[2] * a + white;
            cnt[i]++;
        }
    }
    stbi_image_free(data);
    for (size_t i = 0; i < cnt.size(); i++) {
        if (cnt[i] > 0) {
            c.pix[i * 3] /= cnt[i];
            c.pix[i * 3 + 1] /= cnt[i];
            c.pix[i * 3 + 2] /= cnt[i];
        }
    }
    return c;
}

// Narrows [lo, hi) to the t where -1 < p + d*t < n.
static inline void clip_axis(float p, float d, float n, float& lo, float& hi) {
    if (d > 1e-12f) {
        lo = std::max(lo, (-1 - p) / d);
        hi = std::min(hi, (n - p) / d);
    } else if (d < -1e-12f) {
        lo = std::max(lo, (n - p) / d);
        hi = std::min(hi, (-1 - p) / d);
    } else if (p <= -1 || p >= n) {
        hi = lo;
    }
}

// Calls fn(index, r, g, b, a) for every canvas pixel the shape covers, with
// the premultiplied sprite color at that pixel. index points at the pixel's
// red channel in a 3-channel canvas.
template <class F>
static inline void rasterize(int w, int h, const Shape& sh, const Sprite& sp, float opacity, F&& fn) {
    if (sh.s <= 0) return;
    double cs = std::cos(sh.a), sn = std::sin(sh.a);
    double half = sh.s / 2 * (std::abs(cs) + std::abs(sn));
    int x0 = std::clamp((int)std::floor(sh.x - half), 0, w);
    int x1 = std::clamp((int)std::ceil(sh.x + half), 0, w);
    int y0 = std::clamp((int)std::floor(sh.y - half), 0, h);
    int y1 = std::clamp((int)std::ceil(sh.y + half), 0, h);
    if (x0 >= x1 || y0 >= y1) return;

    // the mip level closest to (but not smaller than) the drawn size
    size_t lv = 0;
    while (lv + 1 < sp.levels.size() && sp.levels[lv + 1].size >= sh.s) lv++;
    const Mip& m = sp.levels[lv];
    const float* tex = m.pix.data();
    const int stride4 = m.stride * 4;
    const float n = (float)m.size;
    const double k = m.size / sh.s;
    // texel coordinate = R(-a) * (p - center) * k + n/2 - 0.5
    const float dux = (float)(cs * k), duy = (float)(sn * k);
    const float dvx = (float)(-sn * k), dvy = (float)(cs * k);
    const double c0 = m.size / 2.0 - 0.5;

    for (int y = y0; y < y1; y++) {
        double py = y + 0.5 - sh.y, px = x0 + 0.5 - sh.x;
        float u0 = (float)(px * dux + py * duy + c0);
        float v0 = (float)(px * dvx + py * dvy + c0);
        // only visit the part of the row that maps inside the sprite
        float lo = 0, hi = (float)(x1 - x0);
        clip_axis(u0, dux, n, lo, hi);
        clip_axis(v0, dvx, n, lo, hi);
        int ts = std::max(0, (int)std::floor(lo));
        int te = std::min(x1 - x0, (int)std::ceil(hi));
        for (int t = ts; t < te; t++) {
            float u = u0 + dux * t, v = v0 + dvx * t;
            if (!(u > -1 && v > -1 && u < n && v < n)) continue;
            float fu = std::floor(u), fv = std::floor(v);
            float tx = u - fu, ty = v - fv;
            const float* p00 = tex + ((int)fv + 1) * stride4 + ((int)fu + 1) * 4;
            const float* p01 = p00 + stride4;
            float w00 = (1 - tx) * (1 - ty), w10 = tx * (1 - ty), w01 = (1 - tx) * ty, w11 = tx * ty;
            float a = p00[3] * w00 + p00[7] * w10 + p01[3] * w01 + p01[7] * w11;
            if (a <= 0) continue;
            float r = p00[0] * w00 + p00[4] * w10 + p01[0] * w01 + p01[4] * w11;
            float g = p00[1] * w00 + p00[5] * w10 + p01[1] * w01 + p01[5] * w11;
            float b = p00[2] * w00 + p00[6] * w10 + p01[2] * w01 + p01[6] * w11;
            fn(((size_t)y * w + x0 + t) * 3, r * opacity, g * opacity, b * opacity, a * opacity);
        }
    }
}

static void draw_shape(Canvas& c, Shape sh, const Sprite& sp, float opacity, double scale) {
    sh.x *= scale;
    sh.y *= scale;
    sh.s *= scale;
    float* pix = c.pix.data();
    rasterize(c.w, c.h, sh, sp, opacity, [pix](size_t i, float r, float g, float b, float a) {
        float ia = 1 - a;
        pix[i] = r + ia * pix[i];
        pix[i + 1] = g + ia * pix[i + 1];
        pix[i + 2] = b + ia * pix[i + 2];
    });
}

Model::Model(Canvas target, const std::vector<Sprite>& sprites, Color bg, int workers, uint64_t seed)
    : target_(std::move(target)), sprites_(sprites), bg_(bg) {
    const int w = target_.w, h = target_.h;
    current_ = Canvas(w, h, bg);
    max_size = std::max(w, h) / 4.0;
    max_angle = kPi / 4;
    for (size_t i = 0; i < current_.pix.size(); i++) {
        double d = current_.pix[i] - target_.pix[i];
        sse_ += d * d;
    }
    sat_.assign((size_t)(w + 1) * (h + 1) * 3, 0.0);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            for (int c = 0; c < 3; c++) {
                sat_[((size_t)(y + 1) * (w + 1) + x + 1) * 3 + c] =
                    target_.pix[((size_t)y * w + x) * 3 + c] + sat_[((size_t)y * (w + 1) + x + 1) * 3 + c] +
                    sat_[((size_t)(y + 1) * (w + 1) + x) * 3 + c] - sat_[((size_t)y * (w + 1) + x) * 3 + c];
            }
        }
    }
    for (int i = 0; i < std::max(1, workers); i++) rngs_.emplace_back(seed + (uint64_t)i * 7919);
}

double Model::score() const {
    return std::sqrt(sse_ / ((double)target_.w * target_.h * 3)) / 255;
}

Color Model::box_mean(double x, double y, double s) const {
    const int w = target_.w, h = target_.h;
    int x0 = std::clamp((int)(x - s / 2), 0, w - 1);
    int y0 = std::clamp((int)(y - s / 2), 0, h - 1);
    int x1 = std::clamp((int)(x + s / 2) + 1, x0 + 1, w);
    int y1 = std::clamp((int)(y + s / 2) + 1, y0 + 1, h);
    double n = (double)(x1 - x0) * (y1 - y0);
    auto at = [&](int xx, int yy, int c) { return sat_[((size_t)yy * (w + 1) + xx) * 3 + c]; };
    Color out;
    for (int c = 0; c < 3; c++) out[c] = (float)((at(x1, y1, c) - at(x1, y0, c) - at(x0, y1, c) + at(x0, y0, c)) / n);
    return out;
}

double Model::delta(const Shape& sh) const {
    const float* cur = current_.pix.data();
    const float* tgt = target_.pix.data();
    float d = 0;
    rasterize(current_.w, current_.h, sh, sprites_[sh.e], opacity,
              [&](size_t i, float r, float g, float b, float a) {
                  float ia = 1 - a;
                  float c0 = cur[i], c1 = cur[i + 1], c2 = cur[i + 2];
                  float t0 = tgt[i], t1 = tgt[i + 1], t2 = tgt[i + 2];
                  float n0 = r + ia * c0 - t0, n1 = g + ia * c1 - t1, n2 = b + ia * c2 - t2;
                  float o0 = c0 - t0, o1 = c1 - t1, o2 = c2 - t2;
                  d += n0 * n0 + n1 * n1 + n2 * n2 - o0 * o0 - o1 * o1 - o2 * o2;
              });
    return d;
}

void Model::add(const Shape& sh) {
    sse_ += delta(sh);
    draw_shape(current_, sh, sprites_[sh.e], opacity, 1);
    shapes_.push_back(sh);
}

Canvas Model::render(double scale) const {
    Canvas c((int)std::lround(target_.w * scale), (int)std::lround(target_.h * scale), bg_);
    for (const auto& sh : shapes_) draw_shape(c, sh, sprites_[sh.e], opacity, scale);
    return c;
}

Shape Model::random_shape(Rng& rng) const {
    std::uniform_real_distribution<double> u01(0, 1);
    Shape sh;
    sh.x = u01(rng) * target_.w;
    sh.y = u01(rng) * target_.h;
    sh.s = min_size * std::pow(max_size / min_size, u01(rng));  // log-uniform
    sh.a = (u01(rng) * 2 - 1) * max_angle;
    sh.e = nearest_sprite(sprites_, box_mean(sh.x, sh.y, sh.s), 12, rng);
    return sh;
}

Shape Model::mutate(Shape sh, Rng& rng) const {
    std::normal_distribution<double> norm(0, 1);
    int ops = max_angle == 0 ? 3 : 4;
    switch (std::uniform_int_distribution<int>(0, ops - 1)(rng)) {
    case 0:
        sh.x = std::clamp(sh.x + norm(rng) * (2 + sh.s * 0.15), 0.0, (double)target_.w);
        sh.y = std::clamp(sh.y + norm(rng) * (2 + sh.s * 0.15), 0.0, (double)target_.h);
        break;
    case 1:
        sh.s = std::clamp(sh.s * std::exp(norm(rng) * 0.15), min_size, max_size);
        break;
    case 2:
        if (rng() & 1) {
            const auto& sim = sprites_[sh.e].similar;
            if (!sim.empty()) sh.e = sim[std::uniform_int_distribution<size_t>(0, sim.size() - 1)(rng)];
        } else {
            sh.e = nearest_sprite(sprites_, box_mean(sh.x, sh.y, sh.s), 12, rng);
        }
        break;
    case 3:
        sh.a = std::clamp(sh.a + norm(rng) * 0.25, -max_angle, max_angle);
        break;
    }
    return sh;
}

// n random trials followed by hill climbing from the best one.
std::pair<Shape, double> Model::search(Rng& rng, int n, int max_age) const {
    Shape best = random_shape(rng);
    double best_d = delta(best);
    for (int i = 1; i < n; i++) {
        Shape sh = random_shape(rng);
        double d = delta(sh);
        if (d < best_d) {
            best = sh;
            best_d = d;
        }
    }
    for (int age = 0; age < max_age; age++) {
        Shape sh = mutate(best, rng);
        double d = delta(sh);
        if (d < best_d) {
            best = sh;
            best_d = d;
            age = -1;
        }
    }
    return {best, best_d};
}

bool Model::step(int trials, int max_age) {
    const int workers = (int)rngs_.size();
    const int per = std::max(1, (trials + workers - 1) / workers);
    std::vector<std::pair<Shape, double>> res(workers);
    std::vector<std::thread> threads;
    for (int i = 1; i < workers; i++) {
        threads.emplace_back([&, i] { res[i] = search(rngs_[i], per, max_age); });
    }
    res[0] = search(rngs_[0], per, max_age);
    for (auto& t : threads) t.join();
    auto best = *std::min_element(res.begin(), res.end(),
                                  [](const auto& a, const auto& b) { return a.second < b.second; });
    if (best.second >= 0) return false;
    add(best.first);
    return true;
}
