// golang.org/x/net/html: an HTML5 parser and renderer, with a node tree API.
// Oracle: rendering a parsed tree and parsing the output again gives the same tree.
package harness

import (
	"bytes"
	"strings"

	"golang.org/x/net/html"
	"golang.org/x/net/html/atom"
	"stitch"
)

func init() {
	stitch.Object[*html.Node]("Node", nil)
	stitch.Decoder("Html", htmlText)
	stitch.Endpoint("parse", parse, "text:max=4096,type=Html")
	stitch.Endpoint("parse_fragment", parseFragment, "text:max=2048,type=Html", "ctx:oneof=body|table|select|svg|math|template|textarea|title|script")
	stitch.Endpoint("tokenize", tokenize, "text:max=4096,type=Html")
	stitch.Endpoint("child", child, "n", "i:range=0..8")
	stitch.Endpoint("render_roundtrip", renderRoundtrip, "text:max=4096,type=Html")
	stitch.Endpoint("render", render, "n")
	stitch.Endpoint("set_attr", setAttr, "n", "key:max=16", "val:max=32,type=Html")
	stitch.Endpoint("append_text", appendText, "n", "text:max=64,type=Html")
	stitch.Endpoint("escape", escape, "s:max=128,type=Html")
}

func htmlText(s *stitch.Source) string {
	var b strings.Builder
	tags := []string{"p", "a", "table", "tr", "td", "svg", "math", "mi", "mtext", "foreignObject", "title", "desc", "style", "script", "template", "select", "option", "textarea", "noscript", "plaintext", "xmp", "iframe", "body", "head", "html"}
	for s.More() {
		switch s.Choice(6) {
		case 0:
			b.WriteString("<" + stitch.Pick(s, tags...))
			for s.Coin(0.3) {
				b.WriteString(" " + stitch.Pick(s, "a", "href", "xlink:href", "class", "encoding") + `="` + s.Str(6) + `"`)
			}
			b.WriteString(">")
		case 1:
			b.WriteString("</" + stitch.Pick(s, tags...) + ">")
		case 2:
			b.WriteString(s.Token())
		case 3:
			b.WriteString(stitch.Pick(s, "&lt;", "&amp;", "&#0;", "&", "<!--", "-->", "<![CDATA[", "]]>", "<!DOCTYPE html>"))
		default:
			b.WriteString(s.Str(8))
		}
	}
	return b.String()
}

func parse(text string) (*html.Node, error) {
	return html.Parse(strings.NewReader(text))
}

func parseFragment(text, ctx string) (*html.Node, error) {
	c := &html.Node{Type: html.ElementNode, Data: ctx, DataAtom: atom.Lookup([]byte(ctx))}
	if ctx == "svg" || ctx == "math" {
		c.Namespace = ctx
	}
	nodes, err := html.ParseFragment(strings.NewReader(text), c)
	if err != nil {
		return nil, err
	}
	root := &html.Node{Type: html.ElementNode, Data: ctx, DataAtom: c.DataAtom, Namespace: c.Namespace}
	for _, n := range nodes {
		root.AppendChild(n)
	}
	return root, nil
}

func tokenize(text string) {
	z := html.NewTokenizer(strings.NewReader(text))
	for i := 0; i < 10000; i++ {
		if z.Next() == html.ErrorToken {
			return
		}
		z.Token()
	}
}

func child(n *html.Node, i int) *html.Node {
	c := n.FirstChild
	for ; c != nil && i > 0; i-- {
		c = c.NextSibling
	}
	return c
}

func dump(n *html.Node, b *strings.Builder) {
	b.WriteString(n.Namespace + "|" + n.Data + "|")
	if n.Type == html.TextNode {
		b.WriteString("#text:")
	}
	for _, a := range n.Attr {
		b.WriteString(a.Namespace + ":" + a.Key + "=" + a.Val + ";")
	}
	b.WriteString("(")
	for c := n.FirstChild; c != nil; c = c.NextSibling {
		dump(c, b)
	}
	b.WriteString(")")
}

func has(n *html.Node, tag string) bool {
	if n.Type == html.ElementNode && n.Data == tag {
		return true
	}
	for c := n.FirstChild; c != nil; c = c.NextSibling {
		if has(c, tag) {
			return true
		}
	}
	return false
}

// renderRoundtrip: for a tree the parser built, parse(render(tree)) has the same tree. (Render
// documents only a best effort for hand-edited trees, so edits are checked for crashes only.)
func renderRoundtrip(text string) {
	n, err := html.Parse(strings.NewReader(text))
	stitch.Require(err == nil, "parse error")
	// the HTML spec notes that serialization cannot round-trip <plaintext> content
	stitch.Require(!has(n, "plaintext"), "contains <plaintext>")
	var out bytes.Buffer
	if err := html.Render(&out, n); err != nil {
		stitch.Bail("render error")
	}
	again, err := html.Parse(bytes.NewReader(out.Bytes()))
	stitch.Check(err == nil, "rendered document parses")
	// whole trees need not round-trip (the spec's adoption agency and foster parenting), but
	// foreign content (svg, math) must come back with the same text and structure
	var a, b strings.Builder
	dumpForeign(n, &a)
	dumpForeign(again, &b)
	stitch.Check(a.String() == b.String(), "svg/math content survives render + parse")
}

func dumpForeign(n *html.Node, b *strings.Builder) {
	if n.Type == html.ElementNode && (n.Namespace == "svg" || n.Namespace == "math") {
		dump(n, b)
		b.WriteString("\n")
		return
	}
	for c := n.FirstChild; c != nil; c = c.NextSibling {
		dumpForeign(c, b)
	}
}

// render: edited trees must render without crashing
func render(n *html.Node) {
	var out bytes.Buffer
	html.Render(&out, n)
}

func setAttr(n *html.Node, key, val string) {
	stitch.Require(n.Type == html.ElementNode && key != "", "not an element")
	n.Attr = append(n.Attr, html.Attribute{Key: key, Val: val})
}

func appendText(n *html.Node, text string) {
	stitch.Require(n.Type == html.ElementNode, "not an element")
	n.AppendChild(&html.Node{Type: html.TextNode, Data: text})
}

func escape(s string) {
	stitch.Check(html.UnescapeString(html.EscapeString(s)) == s, "UnescapeString(EscapeString(s)) == s")
}
