package main

import (
	"image"
	"math"
	"math/rand"
	"sync"
)

// Canvas is an RGB float image with values in 0..255.
type Canvas struct {
	W, H int
	Pix  []float32
}

func NewCanvas(w, h int, bg [3]float32) *Canvas {
	c := &Canvas{W: w, H: h, Pix: make([]float32, w*h*3)}
	for i := 0; i < len(c.Pix); i += 3 {
		c.Pix[i], c.Pix[i+1], c.Pix[i+2] = bg[0], bg[1], bg[2]
	}
	return c
}

// CanvasFromImage converts im to a canvas, scaling it (area averaging) so
// that its longest side is at most maxSize.
func CanvasFromImage(im image.Image, maxSize int) *Canvas {
	b := im.Bounds()
	sw, sh := b.Dx(), b.Dy()
	w, h := sw, sh
	if maxSize > 0 && (sw > maxSize || sh > maxSize) {
		if sw >= sh {
			w, h = maxSize, int(math.Max(1, math.Round(float64(sh)*float64(maxSize)/float64(sw))))
		} else {
			w, h = int(math.Max(1, math.Round(float64(sw)*float64(maxSize)/float64(sh)))), maxSize
		}
	}
	c := &Canvas{W: w, H: h, Pix: make([]float32, w*h*3)}
	cnt := make([]float32, w*h)
	for y := 0; y < sh; y++ {
		ty := y * h / sh
		for x := 0; x < sw; x++ {
			tx := x * w / sw
			r, g, bl, a := im.At(b.Min.X+x, b.Min.Y+y).RGBA()
			// composite transparent pixels over white
			wa := float32(65535-a) / 257
			i := ty*w + tx
			c.Pix[i*3+0] += float32(r)/257 + wa
			c.Pix[i*3+1] += float32(g)/257 + wa
			c.Pix[i*3+2] += float32(bl)/257 + wa
			cnt[i]++
		}
	}
	for i, n := range cnt {
		if n > 0 {
			c.Pix[i*3+0] /= n
			c.Pix[i*3+1] /= n
			c.Pix[i*3+2] /= n
		}
	}
	return c
}

func (c *Canvas) Average() [3]float32 {
	var s [3]float64
	for i := 0; i < len(c.Pix); i += 3 {
		s[0] += float64(c.Pix[i])
		s[1] += float64(c.Pix[i+1])
		s[2] += float64(c.Pix[i+2])
	}
	n := float64(c.W * c.H)
	return [3]float32{float32(s[0] / n), float32(s[1] / n), float32(s[2] / n)}
}

func (c *Canvas) Image() *image.RGBA {
	im := image.NewRGBA(image.Rect(0, 0, c.W, c.H))
	for i := 0; i < c.W*c.H; i++ {
		im.Pix[i*4+0] = clamp8(c.Pix[i*3+0])
		im.Pix[i*4+1] = clamp8(c.Pix[i*3+1])
		im.Pix[i*4+2] = clamp8(c.Pix[i*3+2])
		im.Pix[i*4+3] = 255
	}
	return im
}

func clamp8(v float32) uint8 {
	if v <= 0 {
		return 0
	}
	if v >= 255 {
		return 255
	}
	return uint8(v + 0.5)
}

// Shape is one placed emoji: sprite index, center, side length and rotation
// (radians), all in canvas pixels.
type Shape struct {
	E          int
	X, Y, S, A float64
}

// rasterize calls fn for every canvas pixel covered by the shape with the
// premultiplied sprite color and alpha at that pixel.
func rasterize(w, h int, sh Shape, sp *Sprite, opacity float32, fn func(i int, r, g, b, a float32)) {
	cos, sin := math.Cos(sh.A), math.Sin(sh.A)
	half := sh.S / 2 * (math.Abs(cos) + math.Abs(sin))
	x0 := clampInt(int(math.Floor(sh.X-half)), 0, w)
	x1 := clampInt(int(math.Ceil(sh.X+half)), 0, w)
	y0 := clampInt(int(math.Floor(sh.Y-half)), 0, h)
	y1 := clampInt(int(math.Ceil(sh.Y+half)), 0, h)
	if x0 >= x1 || y0 >= y1 || sh.S <= 0 {
		return
	}

	// pick the mip level closest to (but not smaller than) the drawn size
	lv := 0
	for lv+1 < len(sp.Levels) && float64(sp.Levels[lv+1].Size) >= sh.S {
		lv++
	}
	m := &sp.Levels[lv]
	n := m.Size
	k := float64(n) / sh.S
	// texture coordinate (in texels) = R(-A) * (p - center) * k + n/2 - 0.5
	dux, duy := cos*k, sin*k
	dvx, dvy := -sin*k, cos*k
	fn0 := float64(n)/2 - 0.5

	for y := y0; y < y1; y++ {
		py := float64(y) + 0.5 - sh.Y
		px := float64(x0) + 0.5 - sh.X
		u := px*dux + py*duy + fn0
		v := px*dvx + py*dvy + fn0
		for x := x0; x < x1; x, u, v = x+1, u+dux, v+dvx {
			if u <= -1 || v <= -1 || u >= float64(n) || v >= float64(n) {
				continue
			}
			fx, fy := math.Floor(u), math.Floor(v)
			ix, iy := int(fx), int(fy)
			tx, ty := float32(u-fx), float32(v-fy)
			var r, g, b, a float32
			sample := func(sx, sy int, wt float32) {
				if sx < 0 || sy < 0 || sx >= n || sy >= n || wt == 0 {
					return
				}
				j := (sy*n + sx) * 4
				r += m.Pix[j] * wt
				g += m.Pix[j+1] * wt
				b += m.Pix[j+2] * wt
				a += m.Pix[j+3] * wt
			}
			sample(ix, iy, (1-tx)*(1-ty))
			sample(ix+1, iy, tx*(1-ty))
			sample(ix, iy+1, (1-tx)*ty)
			sample(ix+1, iy+1, tx*ty)
			if a <= 0 {
				continue
			}
			fn((y*w+x)*3, r*opacity, g*opacity, b*opacity, a*opacity)
		}
	}
}

func clampInt(v, lo, hi int) int {
	if v < lo {
		return lo
	}
	if v > hi {
		return hi
	}
	return v
}

// Model holds the optimization state.
type Model struct {
	Target   *Canvas
	Current  *Canvas
	Sprites  []*Sprite
	BG       [3]float32
	Opacity  float32
	MinSize  float64
	MaxSize  float64
	MaxAngle float64
	Shapes   []Shape
	SSE      float64 // sum of squared errors between Current and Target

	sat     []float64 // summed area table of Target, (W+1)*(H+1)*3
	workers []*rand.Rand
}

func NewModel(target *Canvas, sprites []*Sprite, bg [3]float32, workers int, seed int64) *Model {
	m := &Model{
		Target:   target,
		Current:  NewCanvas(target.W, target.H, bg),
		Sprites:  sprites,
		BG:       bg,
		Opacity:  1,
		MinSize:  4,
		MaxSize:  float64(max(target.W, target.H)) / 4,
		MaxAngle: math.Pi / 4,
	}
	for i := range m.Current.Pix {
		d := float64(m.Current.Pix[i] - target.Pix[i])
		m.SSE += d * d
	}
	w, h := target.W, target.H
	m.sat = make([]float64, (w+1)*(h+1)*3)
	for y := 0; y < h; y++ {
		for x := 0; x < w; x++ {
			for c := 0; c < 3; c++ {
				m.sat[((y+1)*(w+1)+x+1)*3+c] = float64(target.Pix[(y*w+x)*3+c]) +
					m.sat[(y*(w+1)+x+1)*3+c] + m.sat[((y+1)*(w+1)+x)*3+c] - m.sat[(y*(w+1)+x)*3+c]
			}
		}
	}
	for i := 0; i < workers; i++ {
		m.workers = append(m.workers, rand.New(rand.NewSource(seed+int64(i)*7919)))
	}
	return m
}

// Score is the root mean squared error normalized to 0..1, like primitive.
func (m *Model) Score() float64 {
	return math.Sqrt(m.SSE/float64(m.Target.W*m.Target.H*3)) / 255
}

// boxMean is the target's mean color over the square of side s centered at (x, y).
func (m *Model) boxMean(x, y, s float64) [3]float32 {
	w, h := m.Target.W, m.Target.H
	x0 := clampInt(int(x-s/2), 0, w-1)
	y0 := clampInt(int(y-s/2), 0, h-1)
	x1 := clampInt(int(x+s/2)+1, x0+1, w)
	y1 := clampInt(int(y+s/2)+1, y0+1, h)
	n := float64((x1 - x0) * (y1 - y0))
	var out [3]float32
	for c := 0; c < 3; c++ {
		v := m.sat[(y1*(w+1)+x1)*3+c] - m.sat[(y0*(w+1)+x1)*3+c] - m.sat[(y1*(w+1)+x0)*3+c] + m.sat[(y0*(w+1)+x0)*3+c]
		out[c] = float32(v / n)
	}
	return out
}

// Delta returns the change in SSE that drawing sh would cause.
func (m *Model) Delta(sh Shape) float64 {
	cur, tgt := m.Current.Pix, m.Target.Pix
	var d float32
	rasterize(m.Current.W, m.Current.H, sh, m.Sprites[sh.E], m.Opacity, func(i int, r, g, b, a float32) {
		ia := 1 - a
		c0, c1, c2 := cur[i], cur[i+1], cur[i+2]
		t0, t1, t2 := tgt[i], tgt[i+1], tgt[i+2]
		n0, n1, n2 := r+ia*c0-t0, g+ia*c1-t1, b+ia*c2-t2
		o0, o1, o2 := c0-t0, c1-t1, c2-t2
		d += n0*n0 + n1*n1 + n2*n2 - o0*o0 - o1*o1 - o2*o2
	})
	return float64(d)
}

// Add draws sh onto the current canvas.
func (m *Model) Add(sh Shape) {
	m.SSE += m.Delta(sh)
	drawShape(m.Current, sh, m.Sprites[sh.E], m.Opacity, 1)
	m.Shapes = append(m.Shapes, sh)
}

func drawShape(c *Canvas, sh Shape, sp *Sprite, opacity float32, scale float64) {
	sh.X *= scale
	sh.Y *= scale
	sh.S *= scale
	pix := c.Pix
	rasterize(c.W, c.H, sh, sp, opacity, func(i int, r, g, b, a float32) {
		ia := 1 - a
		pix[i] = r + ia*pix[i]
		pix[i+1] = g + ia*pix[i+1]
		pix[i+2] = b + ia*pix[i+2]
	})
}

// Render redraws all shapes at the given scale.
func (m *Model) Render(scale float64) *image.RGBA {
	w := int(math.Round(float64(m.Target.W) * scale))
	h := int(math.Round(float64(m.Target.H) * scale))
	c := NewCanvas(w, h, m.BG)
	for _, sh := range m.Shapes {
		drawShape(c, sh, m.Sprites[sh.E], m.Opacity, scale)
	}
	return c.Image()
}

func (m *Model) randomShape(rnd *rand.Rand) Shape {
	sh := Shape{
		X: rnd.Float64() * float64(m.Target.W),
		Y: rnd.Float64() * float64(m.Target.H),
		S: m.MinSize * math.Pow(m.MaxSize/m.MinSize, rnd.Float64()),
		A: (rnd.Float64()*2 - 1) * m.MaxAngle,
	}
	sh.E = nearestSprite(m.Sprites, m.boxMean(sh.X, sh.Y, sh.S), 12, rnd.Intn)
	return sh
}

func (m *Model) mutate(sh Shape, rnd *rand.Rand) Shape {
	w, h := float64(m.Target.W), float64(m.Target.H)
	ops := 4
	if m.MaxAngle == 0 {
		ops = 3
	}
	switch rnd.Intn(ops) {
	case 0:
		sh.X = math.Max(0, math.Min(w, sh.X+rnd.NormFloat64()*(2+sh.S*0.15)))
		sh.Y = math.Max(0, math.Min(h, sh.Y+rnd.NormFloat64()*(2+sh.S*0.15)))
	case 1:
		sh.S = math.Max(m.MinSize, math.Min(m.MaxSize, sh.S*math.Exp(rnd.NormFloat64()*0.15)))
	case 2:
		if rnd.Intn(2) == 0 {
			sh.E = m.Sprites[sh.E].Similar[rnd.Intn(len(m.Sprites[sh.E].Similar))]
		} else {
			sh.E = nearestSprite(m.Sprites, m.boxMean(sh.X, sh.Y, sh.S), 12, rnd.Intn)
		}
	case 3:
		sh.A = math.Max(-m.MaxAngle, math.Min(m.MaxAngle, sh.A+rnd.NormFloat64()*0.25))
	}
	return sh
}

// search runs n random trials followed by hill climbing on the best one.
func (m *Model) search(rnd *rand.Rand, n, maxAge int) (Shape, float64) {
	best := m.randomShape(rnd)
	bestD := m.Delta(best)
	for i := 1; i < n; i++ {
		sh := m.randomShape(rnd)
		if d := m.Delta(sh); d < bestD {
			best, bestD = sh, d
		}
	}
	for age := 0; age < maxAge; age++ {
		sh := m.mutate(best, rnd)
		if d := m.Delta(sh); d < bestD {
			best, bestD = sh, d
			age = -1
		}
	}
	return best, bestD
}

// Step finds and adds one emoji. It returns false if no improving placement
// was found.
func (m *Model) Step(trials, maxAge int) bool {
	type result struct {
		sh Shape
		d  float64
	}
	res := make([]result, len(m.workers))
	per := (trials + len(m.workers) - 1) / len(m.workers)
	var wg sync.WaitGroup
	for i, rnd := range m.workers {
		wg.Add(1)
		go func(i int, rnd *rand.Rand) {
			defer wg.Done()
			sh, d := m.search(rnd, per, maxAge)
			res[i] = result{sh, d}
		}(i, rnd)
	}
	wg.Wait()
	best := res[0]
	for _, r := range res[1:] {
		if r.d < best.d {
			best = r
		}
	}
	if best.d >= 0 {
		return false
	}
	m.Add(best.sh)
	return true
}
