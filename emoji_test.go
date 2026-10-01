package main

import (
	"reflect"
	"testing"
)

func TestSplitEmoji(t *testing.T) {
	got := splitEmoji("🍎❤️👨🏽‍💻🇺🇸 #️⃣")
	want := []string{"1F34E", "2764-FE0F", "1F468-1F3FD-200D-1F4BB", "1F1FA-1F1F8", "0023-FE0F-20E3"}
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("splitEmoji = %v, want %v", got, want)
	}
}

func TestNormalizeUnified(t *testing.T) {
	if a, b := normalizeUnified("2764-fe0f"), normalizeUnified("2764"); a != b {
		t.Fatalf("%q != %q", a, b)
	}
	if got := unifiedToString("1F1FA-1F1F8"); got != "🇺🇸" {
		t.Fatalf("unifiedToString = %q", got)
	}
}
