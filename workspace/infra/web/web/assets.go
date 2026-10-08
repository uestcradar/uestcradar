package webassets

import (
	"embed"
	"io/fs"
)

//go:embed dist/*
var assets embed.FS

func Files() fs.FS {
	result, err := fs.Sub(assets, "dist")
	if err != nil {
		panic(err)
	}
	return result
}
