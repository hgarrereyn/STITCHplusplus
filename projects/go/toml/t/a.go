package main

import (
	"fmt"
	"github.com/BurntSushi/toml"
)

func main() {
	var v map[string]interface{}
	md, err := toml.Decode("[[p]]\nn = 1\n[[p]]\nn = 2\n[o]\na=1\n", &v)
	fmt.Println(err)
	for _, k := range md.Keys() {
		fmt.Println(k, md.IsDefined(k...))
	}
	var s struct{ O map[string]int }
	md, _ = toml.Decode("[o]\na=1\n[x]\nb=2\n", &s)
	fmt.Println(md.Undecoded(), md.IsDefined("x"), md.IsDefined("x","b"))
}
