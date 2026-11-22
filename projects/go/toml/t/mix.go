package main

import (
	"bytes"
	"fmt"

	"github.com/BurntSushi/toml"
)

func main() {
	var b bytes.Buffer
	err := toml.NewEncoder(&b).Encode(map[string]interface{}{"k": []interface{}{int64(1), map[string]interface{}{"": int64(2)}}})
	fmt.Printf("%v %q\n", err, b.String())
	var v map[string]interface{}
	_, err = toml.Decode("k = [1, {\"\" = 2}]", &v)
	fmt.Printf("%v %#v\n", err, v)
	err = toml.NewEncoder(&b).Encode(map[string]interface{}{"k": []interface{}{map[string]interface{}{"": int64(2)}, int64(1)}})
	fmt.Println(err)
}
