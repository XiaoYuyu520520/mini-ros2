package main

import (
	"os"
	"path/filepath"

	"fyne.io/fyne/v2"
	"fyne.io/fyne/v2/theme"
)

// uiLanguage is the language the shell speaks. "zh" is the default; "en"
// keeps the original labels. It is set before the window is built.
var uiLanguage = "zh"

// tr picks the Chinese string when the UI is Chinese and the English one
// otherwise. Two strings next to each other are easier to keep honest than a
// key table: a missing translation would be a silent fallback, this cannot
// miss.
func tr(en, zh string) string {
	if uiLanguage == "zh" {
		return zh
	}
	return en
}

// cjkTheme is the dark theme with one font replaced. Fyne's bundled font has
// no Chinese glyphs, so a Chinese label would render as boxes; the face is
// taken from the Windows font directory instead of being embedded, which
// keeps the binary small and follows the system's font.
type cjkTheme struct {
	fyne.Theme
	font fyne.Resource
}

func (t cjkTheme) Font(style fyne.TextStyle) fyne.Resource {
	if t.font != nil {
		return t.font
	}
	return t.Theme.Font(style)
}

// uiTheme is the theme for the configured language. A Chinese UI needs a font
// that has the glyphs; anything else keeps the stock dark theme.
func uiTheme(language string) fyne.Theme {
	base := theme.DarkTheme()
	uiLanguage = language
	if language != "zh" {
		return base
	}
	// Plain TTF first: Fyne parses a single face, not a .ttc collection, so
	// msyh.ttc (which is common) is only a last resort.
	candidates := []string{
		`C:\Windows\Fonts\msyh.ttf`,
		`C:\Windows\Fonts\Deng.ttf`,
		`C:\Windows\Fonts\simhei.ttf`,
		`C:\Windows\Fonts\simkai.ttf`,
		`C:\Windows\Fonts\simfang.ttf`,
	}
	for _, path := range candidates {
		data, err := os.ReadFile(path)
		if err != nil {
			continue
		}
		return cjkTheme{
			Theme: base,
			font:  fyne.NewStaticResource(filepath.Base(path), data),
		}
	}
	return base
}
