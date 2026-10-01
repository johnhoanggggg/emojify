#include <cstdio>
#include <string>
#include <vector>

#include "emoji.h"

static int failures = 0;

#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, \
                         __LINE__, #cond);                             \
            failures++;                                                \
        }                                                              \
    } while (0)

int main() {
    std::vector<std::string> want = {"1F34E", "2764-FE0F", "1F468-1F3FD-200D-1F4BB", "1F1FA-1F1F8", "0023-FE0F-20E3"};
    CHECK(split_emoji("🍎❤️👨🏽‍💻🇺🇸 #️⃣") == want);
    CHECK(split_emoji("🇺🇸🇯🇵").size() == 2);
    CHECK(normalize_unified("2764-fe0f") == normalize_unified("2764"));
    CHECK(unified_to_utf8("1F1FA-1F1F8") == "🇺🇸");
    CHECK(unified_to_utf8("1F468-1F3FD-200D-1F4BB") == "👨🏽‍💻");
    if (failures == 0) std::printf("ok\n");
    return failures == 0 ? 0 : 1;
}
