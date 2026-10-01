package main

import (
	"bufio"
	"fmt"
	"html"
	"image"
	"image/color/palette"
	"image/draw"
	"image/gif"
	"image/jpeg"
	"image/png"
	"math"
	"os"
	"path/filepath"
	"strings"
)

// Save writes the model to path, choosing the format from the extension.
func (m *Model) Save(path string, size int) error {
	scale := float64(size) / float64(max(m.Target.W, m.Target.H))
	switch strings.ToLower(filepath.Ext(path)) {
	case ".svg":
		return writeFile(path, func(w *bufio.Writer) error { return m.writeSVG(w, scale) })
	case ".jpg", ".jpeg":
		im := m.Render(scale)
		return writeFile(path, func(w *bufio.Writer) error { return jpeg.Encode(w, im, &jpeg.Options{Quality: 95}) })
	default:
		im := m.Render(scale)
		return writeFile(path, func(w *bufio.Writer) error { return png.Encode(w, im) })
	}
}

func writeFile(path string, fn func(*bufio.Writer) error) error {
	f, err := os.Create(path)
	if err != nil {
		return err
	}
	w := bufio.NewWriter(f)
	if err := fn(w); err != nil {
		f.Close()
		return err
	}
	if err := w.Flush(); err != nil {
		f.Close()
		return err
	}
	return f.Close()
}

// writeSVG emits each emoji as a text glyph, so it renders with the viewer's
// emoji font (Apple Color Emoji on Apple devices).
func (m *Model) writeSVG(w *bufio.Writer, scale float64) error {
	W, H := float64(m.Target.W)*scale, float64(m.Target.H)*scale
	fmt.Fprintf(w, `<svg xmlns="http://www.w3.org/2000/svg" width="%.0f" height="%.0f" viewBox="0 0 %.0f %.0f">`+"\n", W, H, W, H)
	fmt.Fprintf(w, `<rect width="100%%" height="100%%" fill="rgb(%d,%d,%d)"/>`+"\n", clamp8(m.BG[0]), clamp8(m.BG[1]), clamp8(m.BG[2]))
	fmt.Fprintf(w, `<g font-family="'Apple Color Emoji','Segoe UI Emoji','Noto Color Emoji',sans-serif" text-anchor="middle" dominant-baseline="central"`)
	if m.Opacity < 1 {
		fmt.Fprintf(w, ` fill-opacity="%.3f"`, m.Opacity)
	}
	fmt.Fprintln(w, ">")
	for _, sh := range m.Shapes {
		x, y, s := sh.X*scale, sh.Y*scale, sh.S*scale
		// Apple's glyph roughly fills its em box; nudge the font size so the
		// glyph matches the sprite's footprint.
		fmt.Fprintf(w, `<text x="%.1f" y="%.1f" font-size="%.1f"`, x, y, s*0.82)
		if sh.A != 0 {
			fmt.Fprintf(w, ` transform="rotate(%.1f %.1f %.1f)"`, sh.A*180/math.Pi, x, y)
		}
		fmt.Fprintf(w, ">%s</text>\n", html.EscapeString(m.Sprites[sh.E].Char))
	}
	fmt.Fprintln(w, "</g>\n</svg>")
	return nil
}

// GIFWriter accumulates animation frames.
type GIFWriter struct {
	g gif.GIF
}

func (gw *GIFWriter) AddFrame(im image.Image, delay int) {
	b := im.Bounds()
	p := image.NewPaletted(b, palette.Plan9)
	draw.FloydSteinberg.Draw(p, b, im, image.Point{})
	gw.g.Image = append(gw.g.Image, p)
	gw.g.Delay = append(gw.g.Delay, delay)
}

func (gw *GIFWriter) Save(path string) error {
	return writeFile(path, func(w *bufio.Writer) error { return gif.EncodeAll(w, &gw.g) })
}
