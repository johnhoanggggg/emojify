package main

import (
	"archive/tar"
	"compress/gzip"
	"encoding/json"
	"fmt"
	"image"
	"image/png"
	"io"
	"net/http"
	"os"
	"path"
	"path/filepath"
	"runtime"
	"sort"
	"strconv"
	"strings"
	"sync"
)

// The Apple emoji images come from the emoji-datasource-apple npm package
// (https://github.com/iamcal/emoji-data). They are downloaded on first use and
// cached; they are not redistributed with this repository.
const (
	datasourceVersion = "16.0.0"
	datasourceURL     = "https://registry.npmjs.org/emoji-datasource-apple/-/emoji-datasource-apple-" + datasourceVersion + ".tgz"
)

// Mip is one level of an emoji's mipmap: premultiplied RGBA, colors in
// 0..255 and alpha in 0..1.
type Mip struct {
	Size int
	Pix  []float32
}

// Sprite is a single emoji glyph ready for rendering.
type Sprite struct {
	Name     string
	Unified  string
	Char     string
	Category string
	Levels   []Mip      // Levels[0] is full resolution, each next level is half
	Mean     [3]float32 // alpha-weighted mean color
	Coverage float32    // mean alpha
	Similar  []int      // indices of sprites with the closest mean colors
}

type emojiEntry struct {
	Name        string `json:"name"`
	Unified     string `json:"unified"`
	Image       string `json:"image"`
	Category    string `json:"category"`
	SortOrder   int    `json:"sort_order"`
	HasImgApple bool   `json:"has_img_apple"`
	SkinVars    map[string]struct {
		Unified     string `json:"unified"`
		Image       string `json:"image"`
		HasImgApple bool   `json:"has_img_apple"`
	} `json:"skin_variations"`
}

// EmojiDir returns the directory holding the cached emoji images, downloading
// them if necessary.
func EmojiDir(dir string) (string, error) {
	if dir == "" {
		cache, err := os.UserCacheDir()
		if err != nil {
			cache = os.TempDir()
		}
		dir = filepath.Join(cache, "emojify", "apple-"+datasourceVersion)
	}
	if _, err := os.Stat(filepath.Join(dir, "emoji.json")); err == nil {
		return dir, nil
	}
	fmt.Fprintf(os.Stderr, "downloading Apple emoji images to %s ...\n", dir)
	if err := download(dir); err != nil {
		return "", fmt.Errorf("downloading emoji: %w", err)
	}
	return dir, nil
}

func download(dir string) error {
	resp, err := http.Get(datasourceURL)
	if err != nil {
		return err
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		return fmt.Errorf("GET %s: %s", datasourceURL, resp.Status)
	}
	gz, err := gzip.NewReader(resp.Body)
	if err != nil {
		return err
	}
	tmp := dir + ".partial"
	os.RemoveAll(tmp)
	if err := os.MkdirAll(filepath.Join(tmp, "64"), 0o755); err != nil {
		return err
	}
	tr := tar.NewReader(gz)
	for {
		hdr, err := tr.Next()
		if err == io.EOF {
			break
		}
		if err != nil {
			return err
		}
		var dst string
		switch {
		case hdr.Name == "package/emoji.json":
			dst = filepath.Join(tmp, "emoji.json")
		case hdr.Name == "package/LICENSE":
			dst = filepath.Join(tmp, "LICENSE")
		case strings.HasPrefix(hdr.Name, "package/img/apple/64/") && strings.HasSuffix(hdr.Name, ".png"):
			dst = filepath.Join(tmp, "64", path.Base(hdr.Name))
		default:
			continue
		}
		f, err := os.Create(dst)
		if err != nil {
			return err
		}
		if _, err := io.Copy(f, tr); err != nil {
			f.Close()
			return err
		}
		if err := f.Close(); err != nil {
			return err
		}
	}
	os.RemoveAll(dir)
	return os.Rename(tmp, dir)
}

// SpriteFilter selects which emoji are loaded.
type SpriteFilter struct {
	Only       string // if non-empty, only emoji appearing in this string
	Categories string // comma separated category names (case-insensitive substring)
	SkinTones  bool   // include skin tone variations
	Flags      bool   // include country flags
}

// LoadSprites loads and preprocesses the emoji images in dir.
func LoadSprites(dir string, filter SpriteFilter) ([]*Sprite, error) {
	data, err := os.ReadFile(filepath.Join(dir, "emoji.json"))
	if err != nil {
		return nil, err
	}
	var entries []emojiEntry
	if err := json.Unmarshal(data, &entries); err != nil {
		return nil, err
	}
	sort.Slice(entries, func(i, j int) bool { return entries[i].SortOrder < entries[j].SortOrder })

	only := map[string]bool{}
	for _, seq := range splitEmoji(filter.Only) {
		only[normalizeUnified(seq)] = true
	}
	var cats []string
	for _, c := range strings.Split(filter.Categories, ",") {
		if c = strings.TrimSpace(strings.ToLower(c)); c != "" {
			cats = append(cats, c)
		}
	}

	type job struct{ name, unified, image, category string }
	var jobs []job
	for _, e := range entries {
		if !e.HasImgApple || e.Category == "Component" {
			continue
		}
		if !filter.Flags && e.Category == "Flags" && len(only) == 0 {
			continue
		}
		if len(cats) > 0 {
			ok := false
			for _, c := range cats {
				ok = ok || strings.Contains(strings.ToLower(e.Category), c)
			}
			if !ok {
				continue
			}
		}
		add := func(unified, image string) {
			if len(only) > 0 && !only[normalizeUnified(unified)] {
				return
			}
			jobs = append(jobs, job{e.Name, unified, image, e.Category})
		}
		add(e.Unified, e.Image)
		if filter.SkinTones || len(only) > 0 {
			keys := make([]string, 0, len(e.SkinVars))
			for k := range e.SkinVars {
				keys = append(keys, k)
			}
			sort.Strings(keys)
			for _, k := range keys {
				if v := e.SkinVars[k]; v.HasImgApple {
					add(v.Unified, v.Image)
				}
			}
		}
	}
	if len(jobs) == 0 {
		return nil, fmt.Errorf("no emoji matched the given filters")
	}

	sprites := make([]*Sprite, len(jobs))
	errs := make([]error, len(jobs))
	var wg sync.WaitGroup
	sem := make(chan struct{}, runtime.NumCPU())
	for i, j := range jobs {
		wg.Add(1)
		go func(i int, j job) {
			defer wg.Done()
			sem <- struct{}{}
			defer func() { <-sem }()
			f, err := os.Open(filepath.Join(dir, "64", j.image))
			if err != nil {
				errs[i] = err
				return
			}
			defer f.Close()
			im, err := png.Decode(f)
			if err != nil {
				errs[i] = fmt.Errorf("%s: %w", j.image, err)
				return
			}
			s := newSprite(im)
			s.Name, s.Unified, s.Category = j.name, j.unified, j.category
			s.Char = unifiedToString(j.unified)
			sprites[i] = s
		}(i, j)
	}
	wg.Wait()
	var out []*Sprite
	for i, s := range sprites {
		if errs[i] != nil {
			return nil, errs[i]
		}
		if s.Coverage > 0.01 {
			out = append(out, s)
		}
	}
	linkSimilar(out, 16)
	return out, nil
}

func newSprite(im image.Image) *Sprite {
	b := im.Bounds()
	n := b.Dx()
	if b.Dy() > n {
		n = b.Dy()
	}
	level := Mip{Size: n, Pix: make([]float32, n*n*4)}
	for y := b.Min.Y; y < b.Max.Y; y++ {
		for x := b.Min.X; x < b.Max.X; x++ {
			// RGBA() is already alpha-premultiplied, 16 bit
			r, g, bl, a := im.At(x, y).RGBA()
			i := ((y-b.Min.Y)*n + (x - b.Min.X)) * 4
			level.Pix[i+0] = float32(r) / 257
			level.Pix[i+1] = float32(g) / 257
			level.Pix[i+2] = float32(bl) / 257
			level.Pix[i+3] = float32(a) / 65535
		}
	}
	s := &Sprite{Levels: []Mip{level}}
	for level.Size > 1 {
		level = downsample(level)
		s.Levels = append(s.Levels, level)
	}
	var sum [4]float64
	l0 := s.Levels[0].Pix
	for i := 0; i < len(l0); i += 4 {
		for c := 0; c < 4; c++ {
			sum[c] += float64(l0[i+c])
		}
	}
	if sum[3] > 0 {
		for c := 0; c < 3; c++ {
			s.Mean[c] = float32(sum[c] / sum[3])
		}
	}
	s.Coverage = float32(sum[3] / float64(n*n))
	return s
}

func downsample(m Mip) Mip {
	n := m.Size / 2
	out := Mip{Size: n, Pix: make([]float32, n*n*4)}
	for y := 0; y < n; y++ {
		for x := 0; x < n; x++ {
			for c := 0; c < 4; c++ {
				a := m.Pix[((2*y)*m.Size+2*x)*4+c]
				b := m.Pix[((2*y)*m.Size+2*x+1)*4+c]
				d := m.Pix[((2*y+1)*m.Size+2*x)*4+c]
				e := m.Pix[((2*y+1)*m.Size+2*x+1)*4+c]
				out.Pix[(y*n+x)*4+c] = (a + b + d + e) / 4
			}
		}
	}
	return out
}

// linkSimilar stores, for each sprite, the k sprites with the closest mean color.
func linkSimilar(sprites []*Sprite, k int) {
	if k > len(sprites)-1 {
		k = len(sprites) - 1
	}
	type cand struct {
		i int
		d float32
	}
	for i, s := range sprites {
		cs := make([]cand, 0, len(sprites))
		for j, t := range sprites {
			if i != j {
				cs = append(cs, cand{j, colorDist(s.Mean, t.Mean)})
			}
		}
		sort.Slice(cs, func(a, b int) bool { return cs[a].d < cs[b].d })
		s.Similar = make([]int, k)
		for j := 0; j < k; j++ {
			s.Similar[j] = cs[j].i
		}
	}
}

func colorDist(a, b [3]float32) float32 {
	dr, dg, db := a[0]-b[0], a[1]-b[1], a[2]-b[2]
	return dr*dr + dg*dg + db*db
}

func unifiedToString(u string) string {
	var sb strings.Builder
	for _, p := range strings.Split(u, "-") {
		if r, err := strconv.ParseUint(p, 16, 32); err == nil {
			sb.WriteRune(rune(r))
		}
	}
	return sb.String()
}

// normalizeUnified uppercases and drops variation selectors so that "❤" and
// "❤️" compare equal.
func normalizeUnified(u string) string {
	var parts []string
	for _, p := range strings.Split(strings.ToUpper(u), "-") {
		if p != "FE0F" && p != "" {
			parts = append(parts, p)
		}
	}
	return strings.Join(parts, "-")
}

// splitEmoji splits a string of emoji into unified codepoint sequences.
// Zero-width joiners, variation selectors, skin tone modifiers, keycap marks
// and regional indicator pairs are kept attached to the preceding character.
func splitEmoji(s string) []string {
	var out []string
	var cur []string
	joinNext := false
	regional := 0
	flush := func() {
		if len(cur) > 0 {
			out = append(out, strings.Join(cur, "-"))
		}
		cur, regional = nil, 0
	}
	for _, r := range s {
		if r == ' ' || r == ',' || r == '\n' || r == '\t' {
			flush()
			continue
		}
		hex := strings.ToUpper(strconv.FormatInt(int64(r), 16))
		for len(hex) < 4 {
			hex = "0" + hex
		}
		isRegional := r >= 0x1F1E6 && r <= 0x1F1FF
		attach := joinNext || r == 0x200D || r == 0xFE0F || r == 0x20E3 ||
			(r >= 0x1F3FB && r <= 0x1F3FF) || (r >= 0xE0020 && r <= 0xE007F) ||
			(isRegional && regional == 1)
		if !attach {
			flush()
		}
		cur = append(cur, hex)
		joinNext = r == 0x200D
		if isRegional {
			regional++
		}
	}
	flush()
	return out
}

// nearestSprite returns the index of a random sprite among the n closest in
// mean color to c.
func nearestSprite(sprites []*Sprite, c [3]float32, n int, rnd func(int) int) int {
	if n > len(sprites) {
		n = len(sprites)
	}
	best := make([]int, 0, n+1)
	dist := make([]float32, 0, n+1)
	for i, s := range sprites {
		d := colorDist(s.Mean, c)
		if len(best) == n && d >= dist[n-1] {
			continue
		}
		j := len(best)
		if j == n {
			j--
		} else {
			best = append(best, 0)
			dist = append(dist, 0)
		}
		for j > 0 && dist[j-1] > d {
			best[j], dist[j] = best[j-1], dist[j-1]
			j--
		}
		best[j], dist[j] = i, d
	}
	return best[rnd(len(best))]
}
