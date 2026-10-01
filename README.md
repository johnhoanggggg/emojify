# emojify

Reproduce images with Apple emoji. It works like
[fogleman/primitive](https://github.com/fogleman/primitive), except every
"shape" is an emoji.

Emoji are added one at a time. For each one, emojify tries a few hundred random
placements and keeps the best. Then it hill-climbs by nudging the position,
size, rotation, and choice of emoji, and keeps any change that brings the
picture closer to the target. Candidate emoji are picked by matching their
average color to the target region, so blue areas fill up with blue emoji and
green areas with green ones.

## Usage

```sh
go build
./emojify -i photo.jpg -o out.png -n 500
```

On first run, emojify downloads the Apple emoji images (64×64 PNGs from the
[`emoji-datasource-apple`](https://github.com/iamcal/emoji-data) npm package,
about 100 MB) into your user cache directory, under
`~/.cache/emojify/apple-16.0.0`. They are not stored in this repository.

| Flag | Default | Description |
|---|---|---|
| `-i` | | input image (png, jpg, gif) |
| `-o` | | output path; `.png`, `.jpg`, `.svg` or `.gif`; may be repeated |
| `-n` | 300 | number of emoji |
| `-r` | 256 | resize the input to this size before processing (larger is slower) |
| `-s` | 1024 | output size |
| `-a` | 255 | emoji opacity (1–255) |
| `-bg` | average | background color, hex |
| `-rot` | 45 | max rotation in degrees; `0` keeps emoji upright |
| `-min` / `-max` | 4 / ¼ image | emoji size range, in pixels of the resized input |
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
./emojify -i face.jpg -o face.png -n 800 -rot 0 -emojis "🍎🍊🍋🍌🍉🍇🍓🫐🥝🍑🥥🍆"

# animated build-up plus a vector version
./emojify -i cat.jpg -o cat.gif -o cat.svg -n 400 -nth 5 -s 512
```
