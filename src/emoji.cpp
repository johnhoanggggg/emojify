#include "emoji.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <thread>

#include "json.hpp"
#include "stb_image.h"

namespace fs = std::filesystem;

// The Apple emoji images come from the emoji-datasource-apple npm package
// (https://github.com/iamcal/emoji-data). They are downloaded on first use and
// cached; they are not redistributed with this repository.
static const char* kVersion = "16.0.0";
static const char* kURL =
    "https://registry.npmjs.org/emoji-datasource-apple/-/emoji-datasource-apple-16.0.0.tgz";

static fs::path cache_root() {
    if (const char* x = std::getenv("XDG_CACHE_HOME"); x && *x) return x;
#ifdef _WIN32
    if (const char* x = std::getenv("LOCALAPPDATA"); x && *x) return x;
#endif
    if (const char* h = std::getenv("HOME"); h && *h) {
#ifdef __APPLE__
        return fs::path(h) / "Library" / "Caches";
#else
        return fs::path(h) / ".cache";
#endif
    }
    return fs::temp_directory_path();
}

std::string emoji_dir(const std::string& override_dir) {
    fs::path dir = override_dir.empty()
        ? cache_root() / "emojify" / (std::string("apple-") + kVersion)
        : fs::path(override_dir);
    if (fs::exists(dir / "emoji.json")) return dir.string();

    std::fprintf(stderr, "downloading Apple emoji images to %s ...\n", dir.string().c_str());
    fs::path tmp = dir.string() + ".partial";
    fs::remove_all(tmp);
    fs::create_directories(tmp);
    std::string cmd = "curl -fsSL '" + std::string(kURL) + "' | tar -xz -C '" + tmp.string() +
                      "' package/emoji.json package/LICENSE package/img/apple/64";
    if (std::system(cmd.c_str()) != 0 || !fs::exists(tmp / "package" / "emoji.json")) {
        fs::remove_all(tmp);
        throw std::runtime_error("downloading emoji failed (needs curl and tar): " + std::string(kURL));
    }
    fs::rename(tmp / "package" / "img" / "apple" / "64", tmp / "64");
    fs::rename(tmp / "package" / "emoji.json", tmp / "emoji.json");
    fs::rename(tmp / "package" / "LICENSE", tmp / "LICENSE");
    fs::remove_all(tmp / "package");
    fs::remove_all(dir);
    fs::rename(tmp, dir);
    return dir.string();
}

static std::string upper(std::string s) {
    for (auto& c : s) c = (char)std::toupper((unsigned char)c);
    return s;
}

static std::string lower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

std::string normalize_unified(const std::string& u) {
    std::string out, part;
    std::stringstream ss(upper(u));
    while (std::getline(ss, part, '-')) {
        if (part.empty() || part == "FE0F") continue;
        if (!out.empty()) out += '-';
        out += part;
    }
    return out;
}

static void append_utf8(std::string& s, uint32_t r) {
    if (r < 0x80) {
        s += (char)r;
    } else if (r < 0x800) {
        s += (char)(0xC0 | (r >> 6));
        s += (char)(0x80 | (r & 0x3F));
    } else if (r < 0x10000) {
        s += (char)(0xE0 | (r >> 12));
        s += (char)(0x80 | ((r >> 6) & 0x3F));
        s += (char)(0x80 | (r & 0x3F));
    } else {
        s += (char)(0xF0 | (r >> 18));
        s += (char)(0x80 | ((r >> 12) & 0x3F));
        s += (char)(0x80 | ((r >> 6) & 0x3F));
        s += (char)(0x80 | (r & 0x3F));
    }
}

std::string unified_to_utf8(const std::string& u) {
    std::string out, part;
    std::stringstream ss(u);
    while (std::getline(ss, part, '-')) {
        if (!part.empty()) append_utf8(out, (uint32_t)std::stoul(part, nullptr, 16));
    }
    return out;
}

static std::vector<uint32_t> decode_utf8(const std::string& s) {
    std::vector<uint32_t> out;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = (unsigned char)s[i];
        int n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1;
        uint32_t r = n == 1 ? c : n == 2 ? (c & 0x1F) : n == 3 ? (c & 0x0F) : (c & 0x07);
        for (int k = 1; k < n && i + k < s.size(); k++) r = (r << 6) | ((unsigned char)s[i + k] & 0x3F);
        out.push_back(r);
        i += n;
    }
    return out;
}

// Zero-width joiners, variation selectors, skin tone modifiers, keycap marks,
// tag characters and the second half of a regional indicator pair stay
// attached to the preceding character.
std::vector<std::string> split_emoji(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    bool join_next = false;
    int regional = 0;
    auto flush = [&] {
        if (!cur.empty()) out.push_back(cur);
        cur.clear();
        regional = 0;
    };
    for (uint32_t r : decode_utf8(s)) {
        if (r == ' ' || r == ',' || r == '\n' || r == '\t') {
            flush();
            join_next = false;
            continue;
        }
        bool is_regional = r >= 0x1F1E6 && r <= 0x1F1FF;
        bool attach = join_next || r == 0x200D || r == 0xFE0F || r == 0x20E3 ||
                      (r >= 0x1F3FB && r <= 0x1F3FF) || (r >= 0xE0020 && r <= 0xE007F) ||
                      (is_regional && regional == 1);
        if (!attach) flush();
        char hex[16];
        std::snprintf(hex, sizeof hex, "%04X", r);
        if (!cur.empty()) cur += '-';
        cur += hex;
        join_next = r == 0x200D;
        if (is_regional) regional++;
    }
    flush();
    return out;
}

static Mip downsample(const Mip& m) {
    Mip out;
    out.size = m.size / 2;
    out.stride = out.size + 2;
    out.pix.assign((size_t)out.stride * out.stride * 4, 0.f);
    for (int y = 0; y < out.size; y++) {
        for (int x = 0; x < out.size; x++) {
            for (int c = 0; c < 4; c++) {
                auto at = [&](int sx, int sy) { return m.pix[((size_t)(sy + 1) * m.stride + sx + 1) * 4 + c]; };
                out.pix[((size_t)(y + 1) * out.stride + x + 1) * 4 + c] =
                    (at(2 * x, 2 * y) + at(2 * x + 1, 2 * y) + at(2 * x, 2 * y + 1) + at(2 * x + 1, 2 * y + 1)) / 4;
            }
        }
    }
    return out;
}

static bool make_sprite(const std::string& path, Sprite& s) {
    int w, h, n;
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &n, 4);
    if (!data) return false;
    Mip m;
    m.size = std::max(w, h);
    m.stride = m.size + 2;
    m.pix.assign((size_t)m.stride * m.stride * 4, 0.f);
    double sum[4] = {0, 0, 0, 0};
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const unsigned char* p = data + ((size_t)y * w + x) * 4;
            float a = p[3] / 255.f;
            float* q = &m.pix[((size_t)(y + 1) * m.stride + x + 1) * 4];
            q[0] = p[0] * a;
            q[1] = p[1] * a;
            q[2] = p[2] * a;
            q[3] = a;
            for (int c = 0; c < 4; c++) sum[c] += q[c];
        }
    }
    stbi_image_free(data);
    if (sum[3] > 0) {
        for (int c = 0; c < 3; c++) s.mean[c] = (float)(sum[c] / sum[3]);
    }
    s.coverage = (float)(sum[3] / ((double)m.size * m.size));
    s.levels.push_back(std::move(m));
    while (s.levels.back().size > 1) s.levels.push_back(downsample(s.levels.back()));
    return true;
}

static float color_dist(const Color& a, const Color& b) {
    float dr = a[0] - b[0], dg = a[1] - b[1], db = a[2] - b[2];
    return dr * dr + dg * dg + db * db;
}

std::vector<Sprite> load_sprites(const std::string& dir, const SpriteFilter& filter, int threads) {
    std::ifstream in(fs::path(dir) / "emoji.json");
    if (!in) throw std::runtime_error("cannot read " + dir + "/emoji.json");
    nlohmann::json entries = nlohmann::json::parse(in);
    std::vector<nlohmann::json> sorted(entries.begin(), entries.end());
    std::stable_sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
        return a.value("sort_order", 0) < b.value("sort_order", 0);
    });

    std::set<std::string> only;
    for (const auto& seq : split_emoji(filter.only)) only.insert(normalize_unified(seq));
    std::vector<std::string> cats;
    {
        std::stringstream ss(filter.categories);
        std::string c;
        while (std::getline(ss, c, ',')) {
            c.erase(0, c.find_first_not_of(' '));
            c.erase(c.find_last_not_of(' ') + 1);
            if (!c.empty()) cats.push_back(lower(c));
        }
    }

    struct Job {
        std::string name, unified, image, category;
    };
    std::vector<Job> jobs;
    for (const auto& e : sorted) {
        std::string category = e.value("category", "");
        if (!e.value("has_img_apple", false) || category == "Component") continue;
        if (!filter.flags && category == "Flags" && only.empty()) continue;
        if (!cats.empty()) {
            bool ok = false;
            for (const auto& c : cats) ok = ok || lower(category).find(c) != std::string::npos;
            if (!ok) continue;
        }
        std::string name = e.value("name", "");
        auto add = [&](const std::string& unified, const std::string& image) {
            if (!only.empty() && !only.count(normalize_unified(unified))) return;
            jobs.push_back({name, unified, image, category});
        };
        add(e.value("unified", ""), e.value("image", ""));
        if ((filter.skin_tones || !only.empty()) && e.contains("skin_variations")) {
            // nlohmann::json objects iterate in key order
            for (const auto& [key, v] : e["skin_variations"].items()) {
                if (v.value("has_img_apple", false)) add(v.value("unified", ""), v.value("image", ""));
            }
        }
    }
    if (jobs.empty()) throw std::runtime_error("no emoji matched the given filters");

    std::vector<Sprite> loaded(jobs.size());
    std::vector<char> ok(jobs.size(), 0);
    std::atomic<size_t> next{0};
    std::vector<std::thread> pool;
    for (int t = 0; t < std::max(1, threads); t++) {
        pool.emplace_back([&] {
            for (size_t i; (i = next++) < jobs.size();) {
                Sprite& s = loaded[i];
                ok[i] = make_sprite((fs::path(dir) / "64" / jobs[i].image).string(), s);
                s.name = jobs[i].name;
                s.unified = jobs[i].unified;
                s.category = jobs[i].category;
                s.chars = unified_to_utf8(jobs[i].unified);
            }
        });
    }
    for (auto& t : pool) t.join();

    std::vector<Sprite> out;
    for (size_t i = 0; i < loaded.size(); i++) {
        if (!ok[i]) throw std::runtime_error("cannot decode " + jobs[i].image);
        if (loaded[i].coverage > 0.01f) out.push_back(std::move(loaded[i]));
    }

    // link each sprite to the ones with the most similar mean color
    size_t k = std::min<size_t>(16, out.size() - 1);
    std::vector<std::pair<float, int>> cand;
    for (size_t i = 0; i < out.size(); i++) {
        cand.clear();
        for (size_t j = 0; j < out.size(); j++) {
            if (i != j) cand.push_back({color_dist(out[i].mean, out[j].mean), (int)j});
        }
        std::partial_sort(cand.begin(), cand.begin() + k, cand.end());
        out[i].similar.clear();
        for (size_t j = 0; j < k; j++) out[i].similar.push_back(cand[j].second);
    }
    return out;
}

int nearest_sprite(const std::vector<Sprite>& sprites, const Color& c, int n, Rng& rng) {
    n = std::min<int>(n, (int)sprites.size());
    // small sorted insertion list of the n best
    int best[64];
    float dist[64];
    n = std::min(n, 64);
    int count = 0;
    for (int i = 0; i < (int)sprites.size(); i++) {
        float d = color_dist(sprites[i].mean, c);
        if (count == n && d >= dist[n - 1]) continue;
        int j = count < n ? count++ : n - 1;
        while (j > 0 && dist[j - 1] > d) {
            best[j] = best[j - 1];
            dist[j] = dist[j - 1];
            j--;
        }
        best[j] = i;
        dist[j] = d;
    }
    return best[std::uniform_int_distribution<int>(0, count - 1)(rng)];
}
