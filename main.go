// Command emojify reproduces images with Apple emoji, in the spirit of
// fogleman/primitive: emoji are added one at a time, each chosen by random
// search plus hill climbing to minimize the error against the target image.
package main

import (
	"flag"
	"fmt"
	"image"
	_ "image/gif"
	_ "image/jpeg"
	_ "image/png"
	"math"
	"os"
	"path/filepath"
	"runtime"
	"strconv"
	"strings"
	"time"
)

type outputs []string

func (o *outputs) String() string     { return strings.Join(*o, ",") }
func (o *outputs) Set(v string) error { *o = append(*o, v); return nil }

func main() {
	var (
		outs       outputs
		input      = flag.String("i", "", "input image path (required)")
		count      = flag.Int("n", 300, "number of emoji")
		inputSize  = flag.Int("r", 256, "resize the input to this size before processing")
		outputSize = flag.Int("s", 1024, "output image size")
		alpha      = flag.Int("a", 255, "emoji opacity, 1-255")
		bgFlag     = flag.String("bg", "", "background color as hex (default: image average)")
		workers    = flag.Int("j", runtime.NumCPU(), "number of parallel workers")
		trials     = flag.Int("t", 400, "random candidates tried per emoji (split across workers)")
		maxAge     = flag.Int("age", 100, "hill climbing gives up after this many failed mutations")
		rot        = flag.Float64("rot", 45, "maximum rotation in degrees (0 keeps emoji upright)")
		minSize    = flag.Float64("min", 4, "minimum emoji size, in pixels of the resized input")
		maxSize    = flag.Float64("max", 0, "maximum emoji size, in pixels of the resized input (default: a quarter of the image)")
		nth        = flag.Int("nth", 1, "save every Nth frame (when the output path contains %d, or for .gif)")
		only       = flag.String("emojis", "", `only use these emoji, e.g. "🍎🍊🍋🍏🫐🍇"`)
		categories = flag.String("cat", "", `only use these categories, comma separated (e.g. "food,animals")`)
		skinTones  = flag.Bool("skin", false, "include skin tone variations")
		flags      = flag.Bool("flags", false, "include country flags")
		emojiDir   = flag.String("emoji-dir", "", "directory with cached emoji images (default: user cache dir)")
		seed       = flag.Int64("seed", 0, "random seed (default: time based)")
		verbose    = flag.Bool("v", false, "verbose output")
	)
	flag.Var(&outs, "o", "output image path (.png, .jpg, .svg, .gif); may be repeated")
	flag.Usage = func() {
		fmt.Fprintf(os.Stderr, "usage: emojify -i input.jpg -o output.png [flags]\n\n")
		flag.PrintDefaults()
	}
	flag.Parse()
	if *input == "" || len(outs) == 0 {
		flag.Usage()
		os.Exit(2)
	}
	if err := run(config{
		input: *input, outs: outs, count: *count, inputSize: *inputSize, outputSize: *outputSize,
		alpha: *alpha, bg: *bgFlag, workers: *workers, trials: *trials, maxAge: *maxAge, rot: *rot,
		minSize: *minSize, maxSize: *maxSize, nth: *nth, emojiDir: *emojiDir, seed: *seed, verbose: *verbose,
		filter: SpriteFilter{Only: *only, Categories: *categories, SkinTones: *skinTones, Flags: *flags},
	}); err != nil {
		fmt.Fprintln(os.Stderr, "emojify:", err)
		os.Exit(1)
	}
}

type config struct {
	input                        string
	outs                         []string
	count, inputSize, outputSize int
	alpha, workers, trials       int
	maxAge, nth                  int
	bg, emojiDir                 string
	rot, minSize, maxSize        float64
	seed                         int64
	verbose                      bool
	filter                       SpriteFilter
}

func run(c config) error {
	f, err := os.Open(c.input)
	if err != nil {
		return err
	}
	src, _, err := image.Decode(f)
	f.Close()
	if err != nil {
		return fmt.Errorf("decoding %s: %w", c.input, err)
	}

	dir, err := EmojiDir(c.emojiDir)
	if err != nil {
		return err
	}
	start := time.Now()
	sprites, err := LoadSprites(dir, c.filter)
	if err != nil {
		return err
	}
	if c.verbose {
		fmt.Fprintf(os.Stderr, "loaded %d emoji in %.2fs\n", len(sprites), time.Since(start).Seconds())
	}

	target := CanvasFromImage(src, c.inputSize)
	bg := target.Average()
	if c.bg != "" {
		if bg, err = parseHex(c.bg); err != nil {
			return err
		}
	}
	if c.seed == 0 {
		c.seed = time.Now().UnixNano()
	}
	if c.workers < 1 {
		c.workers = 1
	}
	m := NewModel(target, sprites, bg, c.workers, c.seed)
	m.Opacity = float32(clampInt(c.alpha, 1, 255)) / 255
	m.MaxAngle = c.rot * math.Pi / 180
	m.MinSize = math.Max(1, c.minSize)
	if c.maxSize > 0 {
		m.MaxSize = c.maxSize
	}
	m.MaxSize = math.Max(m.MaxSize, m.MinSize)

	var gifs []*GIFWriter
	var gifPaths []string
	for _, o := range c.outs {
		if strings.EqualFold(filepath.Ext(o), ".gif") {
			gifs = append(gifs, &GIFWriter{})
			gifPaths = append(gifPaths, o)
		}
	}
	gifScale := float64(c.outputSize) / float64(max(target.W, target.H))

	start = time.Now()
	fails := 0
	for i := 1; i <= c.count; i++ {
		if !m.Step(c.trials, c.maxAge) {
			// nothing improved this round; retry a few times before giving up
			fails++
			if fails > 10 {
				fmt.Fprintf(os.Stderr, "stopping early at %d emoji: no further improvement found\n", len(m.Shapes))
				break
			}
			i--
			continue
		}
		fails = 0
		if c.verbose {
			sh := m.Shapes[len(m.Shapes)-1]
			fmt.Fprintf(os.Stderr, "%d: t=%.3f, score=%.6f, %s %s\n", i, time.Since(start).Seconds(), m.Score(),
				sprites[sh.E].Char, strings.ToLower(sprites[sh.E].Name))
		}
		last := i == c.count
		if i%c.nth == 0 || last {
			if len(gifs) > 0 {
				frame := m.Render(gifScale)
				delay := 4
				if last {
					delay = 300
				}
				for _, g := range gifs {
					g.AddFrame(frame, delay)
				}
			}
			for _, o := range c.outs {
				if strings.Contains(o, "%") && !strings.EqualFold(filepath.Ext(o), ".gif") {
					if err := m.Save(fmt.Sprintf(o, i), c.outputSize); err != nil {
						return err
					}
				}
			}
		}
	}

	for _, o := range c.outs {
		ext := strings.ToLower(filepath.Ext(o))
		if ext == ".gif" || strings.Contains(o, "%") {
			continue
		}
		if err := m.Save(o, c.outputSize); err != nil {
			return err
		}
	}
	for i, g := range gifs {
		if err := g.Save(gifPaths[i]); err != nil {
			return err
		}
	}
	fmt.Fprintf(os.Stderr, "%d emoji, score %.6f, %.1fs\n", len(m.Shapes), m.Score(), time.Since(start).Seconds())
	return nil
}

func parseHex(s string) ([3]float32, error) {
	s = strings.TrimPrefix(s, "#")
	if len(s) == 3 {
		s = string([]byte{s[0], s[0], s[1], s[1], s[2], s[2]})
	}
	v, err := strconv.ParseUint(s, 16, 32)
	if err != nil || len(s) != 6 {
		return [3]float32{}, fmt.Errorf("invalid color %q", s)
	}
	return [3]float32{float32(v >> 16 & 255), float32(v >> 8 & 255), float32(v & 255)}, nil
}
