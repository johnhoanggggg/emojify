# emojify

Reproduce images with Apple emoji. It works like
[fogleman/primitive](https://github.com/fogleman/primitive), except every
"shape" is an emoji.

Emoji are added one at a time. For each one, emojify tries a few hundred random
placements and keeps the best. Then it hill-climbs by nudging the position,
size, rotation, and choice of emoji, and keeps any change that brings the
picture closer to the target. Candidate emoji are picked by matching their
average color to the target region, so blue areas fill up with blue emoji and
red areas with red ones.

## Build

Requires a C++17 compiler and CMake. All libraries are vendored in
`third_party/`
([stb_image](https://github.com/nothings/stb),
[gif-h](https://github.com/charlietangora/gif-h),
[nlohmann/json](https://github.com/nlohmann/json)).

```sh
cmake -S . -B build
cmake --build build -j
./build/emojify -i photo.jpg -o out.png
```

By default the build uses `-march=native`. Pass `-DEMOJIFY_NATIVE=OFF` for a
binary that runs on other machines.

On first run, emojify downloads the Apple emoji images (64×64 PNGs from the
[`emoji-datasource-apple`](https://github.com/iamcal/emoji-data) npm package,
about 100 MB) with `curl` and `tar`. It caches them in
`~/.cache/emojify/apple-16.0.0` (`~/Library/Caches/...` on macOS). They are not
stored in this repository.

To try it on some classic test photos:

```sh
./scripts/fetch-samples.sh   # astronaut, cat, coffee, rocket, Starry Night, fruits, baboon
./build/emojify -i samples/starry_night.jpg -o starry.png
```

## Flags

| Flag | Default | Description |
|---|---|---|
| `-i` | | input image (png, jpg, gif, bmp, …) |
| `-o` | | output path; `.png`, `.jpg`, `.svg` or `.gif`; may be repeated |
| `-n` | 1000 | number of emoji |
| `-r` | 320 | resize the input to this size before processing (larger is slower) |
| `-s` | 2048 | output size |
| `-a` | 255 | emoji opacity (1–255) |
| `-bg` | average | background color, hex |
| `-rot` | 45 | max rotation in degrees; `0` keeps emoji upright |
| `-min` / `-max` | 24 / ¼ image | emoji size range, in pixels of the resized input; lower `-min` (e.g. 8) for more detail |
| `-emojis` | all | only use these, e.g. `-emojis "🍎🍊🍋🍏🫐🍇"` |
| `-cat` | all | only use these categories, e.g. `-cat food,animals` |
| `-skin` | off | include skin tone variations |
| `-flags` | off | include country flags |
| `-t` | 400 | random candidates per emoji |
| `-age` | 100 | hill climbing stops after this many failed mutations in a row |
| `-j` | #CPUs | parallel workers |
| `-nth` | 1 | with `%d` in the output path (e.g. `frame%03d.png`) or a `.gif`, save every Nth frame |
| `-seed` | time | random seed |
| `-v` | off | print each emoji as it is placed |

### Output formats

- **png / jpg**: rendered from the Apple emoji bitmaps.
- **gif**: an animation of the image being built up, one frame every `-nth`
  emoji.
- **svg**: each emoji is a `<text>` glyph, so the file is tiny and shows up in
  Apple Color Emoji on Apple devices. Other platforms substitute their own emoji
  font.

### Examples

```sh
# fruit-only portrait, upright emoji
./build/emojify -i face.jpg -o face.png -n 800 -rot 0 -emojis "🍎🍊🍋🍌🍉🍇🍓🫐🥝🍑🥥🍆"

# animated build-up plus a vector version
./build/emojify -i cat.jpg -o cat.gif -o cat.svg -n 400 -nth 5 -s 512
```

## Performance

At the default settings (up to 1000 emoji, 2048px output) a run takes about
10–25 seconds on 4 cores. With the 24px minimum emoji size, many images stop
early once more emoji would only make the match worse. Most of that time goes into scoring candidate placements.
For a faster, rougher result, try `-r 200` or `-t 200 -age 50`.
