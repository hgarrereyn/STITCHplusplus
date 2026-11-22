package main

import (
	"fmt"

	"github.com/BurntSushi/toml"
)

func main() {
	var v map[string]interface{}
	_, err := toml.Decode("n=''''''\xae", &v)
	fmt.Println(err, v)
}
