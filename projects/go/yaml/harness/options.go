// Option sets for Load/Dump/Loader/Dumper, built from fuzzer choices: every With* option, the
// version presets, Options(), and the official plugins (loader limits, merge keys, alias data, tab
// indentation, error formats).
package harness

import (
	"errors"
	"fmt"
	"strings"

	"go.yaml.in/yaml/v4"
	"go.yaml.in/yaml/v4/plugin/aliasdata"
	errorformatv3 "go.yaml.in/yaml/v4/plugin/errorformat/v3"
	errorformatv4 "go.yaml.in/yaml/v4/plugin/errorformat/v4"
	"go.yaml.in/yaml/v4/plugin/loaderlimits"
	"go.yaml.in/yaml/v4/plugin/mergekey"
	"go.yaml.in/yaml/v4/plugin/tabindent"
	"stitch"
)

// Opts is a list of options plus what the oracles need to know about it.
type Opts struct {
	List []yaml.Option
	// Plain: only output-formatting options whose documented effect keeps the loaded value the
	// same (no canonical output, tab indentation, merge/alias plugins or document-mode options).
	Plain bool
	// Stream: options that change how many documents Load and Loader see.
	Stream bool
	Desc   []string
}

func (o *Opts) add(plain bool, desc string, opt yaml.Option) {
	o.List = append(o.List, opt)
	o.Plain = o.Plain && plain
	o.Desc = append(o.Desc, desc)
}

func (o Opts) String() string { return strings.Join(o.Desc, ",") }

func optBool(s *stitch.Source) bool { return s.Choice(2) == 0 }

// formatOpt: an output-formatting option (all keep the value of a dumped document)
func formatOpt(s *stitch.Source, o *Opts) {
	switch s.Choice(11) {
	case 0:
		n := int(s.Range(-1, 10))
		o.add(true, fmt.Sprintf("indent(%d)", n), yaml.WithIndent(n))
	case 1:
		if s.Choice(2) == 0 {
			o.add(true, "compact()", yaml.WithCompactSeqIndent())
		} else {
			b := optBool(s)
			o.add(true, fmt.Sprintf("compact(%v)", b), yaml.WithCompactSeqIndent(b))
		}
	case 2:
		n := int(stitch.Pick(s, -1, 0, 1, 5, 20, 40, 80))
		o.add(true, fmt.Sprintf("width(%d)", n), yaml.WithLineWidth(n))
	case 3:
		b := optBool(s)
		o.add(true, fmt.Sprintf("unicode(%v)", b), yaml.WithUnicode(b))
	case 4:
		lb := stitch.Pick(s, yaml.LineBreakLN, yaml.LineBreakCR, yaml.LineBreakCRLN)
		o.add(true, fmt.Sprintf("linebreak(%d)", lb), yaml.WithLineBreak(lb))
	case 5:
		b := optBool(s)
		o.add(true, fmt.Sprintf("start(%v)", b), yaml.WithExplicitStart(b))
	case 6:
		b := optBool(s)
		o.add(true, fmt.Sprintf("end(%v)", b), yaml.WithExplicitEnd(b))
	case 7:
		b := optBool(s)
		o.add(true, fmt.Sprintf("flowsimple(%v)", b), yaml.WithFlowSimpleCollections(b))
	case 8:
		q := stitch.Pick(s, yaml.QuoteSingle, yaml.QuoteDouble, yaml.QuoteLegacy)
		o.add(true, fmt.Sprintf("quote(%d)", q), yaml.WithQuotePreference(q))
	case 9:
		switch s.Choice(3) {
		case 0:
			o.add(true, "v2", yaml.WithV2Defaults())
		case 1:
			o.add(true, "v3", yaml.WithV3Defaults())
		default:
			o.add(true, "v4", yaml.WithV4Defaults())
		}
	default:
		o.add(false, "canonical", yaml.WithCanonical(optBool(s)))
	}
}

func loadOpt(s *stitch.Source, o *Opts) {
	switch s.Choice(7) {
	case 0:
		b := optBool(s)
		o.add(true, fmt.Sprintf("known(%v)", b), yaml.WithKnownFields(b))
	case 1:
		b := optBool(s)
		o.add(true, fmt.Sprintf("unique(%v)", b), yaml.WithUniqueKeys(b))
	case 2:
		o.Stream = true
		o.add(true, "single", yaml.WithSingleDocument(optBool(s)))
	case 3:
		o.Stream = true
		o.add(true, "streamnodes", yaml.WithStreamNodes(optBool(s)))
	case 4:
		o.Stream = true
		o.add(true, "alldocs", yaml.WithAllDocuments(optBool(s)))
	case 5:
		switch s.Choice(3) {
		case 0:
			o.add(true, "v2", yaml.WithV2Defaults())
		case 1:
			o.add(true, "v3", yaml.WithV3Defaults())
		default:
			o.add(true, "v4", yaml.WithV4Defaults())
		}
	default:
		pluginOpt(s, o)
	}
}

var errLimit = errors.New("harness limit")

// pluginOpt: one of the official plugins through WithPlugin, or a plugin chosen by name.
// (loaderlimits.AliasNone is left out: it turns off the documented protection against alias
// expansion bombs, so a timeout with it would be by design.)
func pluginOpt(s *stitch.Source, o *Opts) {
	switch s.Choice(7) {
	case 0:
		var lo []loaderlimits.Option
		for n := s.Choice(3); n > 0; n-- {
			switch s.Choice(5) {
			case 0:
				lo = append(lo, loaderlimits.AliasValue(int(s.Range(-1, 200))))
			case 1:
				lo = append(lo, loaderlimits.DepthValue(int(s.Range(-1, 200))))
			case 2:
				lo = append(lo, loaderlimits.DepthNone())
			case 3:
				max := int(s.Range(0, 64))
				lo = append(lo, loaderlimits.DepthFunc(func(depth int, ctx *yaml.DepthContext) error {
					_ = ctx.Kind == yaml.DepthKindFlow || ctx.Kind == yaml.DepthKindBlock
					if depth > max {
						return errLimit
					}
					return nil
				}))
			default:
				max := int(s.Range(0, 1000))
				lo = append(lo, loaderlimits.AliasFunc(func(aliasCount, constructCount int) error {
					if aliasCount > max {
						return errLimit
					}
					return nil
				}))
			}
		}
		o.add(true, "loaderlimits", yaml.WithPlugin(loaderlimits.New(lo...)))
	case 1:
		var mo []mergekey.Option
		for n := s.Choice(3); n > 0; n-- {
			switch s.Choice(3) {
			case 0:
				mo = append(mo, mergekey.WithConflicts(stitch.Pick(s, "first", "last", "error", "bogus")))
			case 1:
				mo = append(mo, mergekey.WithMultiple(stitch.Pick(s, "error", "combine", "")))
			default:
				mo = append(mo, mergekey.WithDisable(optBool(s)))
			}
		}
		o.add(false, "mergekey", yaml.WithPlugin(mergekey.New(mo...)))
	case 2:
		var ao []aliasdata.Option
		for n := s.Choice(3); n > 0; n-- {
			switch s.Choice(5) {
			case 0:
				ao = append(ao, aliasdata.WithData(map[string]any{
					stitch.Pick(s, "a", "b", "x.y", "*"): stitch.Pick[any](s, 1, "s", nil, []any{1, "t"}, map[string]any{"k": []any{2}}),
				}))
			case 1:
				ao = append(ao, aliasdata.WithPath(optBool(s)))
			case 2:
				ao = append(ao, aliasdata.WithStream(optBool(s)))
			case 3:
				ao = append(ao, aliasdata.WithDisable(optBool(s)))
			default:
				ao = append(ao, aliasdata.WithEnv(stitch.Pick[any](s, false, "STITCH_NO_SUCH_*", []string{"STITCH_NONE"})))
			}
		}
		p, err := aliasdata.New(ao...)
		if err != nil {
			return
		}
		o.add(false, "aliasdata", yaml.WithPlugin(p))
	case 3:
		var to []tabindent.Option
		for n := s.Choice(3); n > 0; n-- {
			switch s.Choice(4) {
			case 0:
				to = append(to, tabindent.WithMode(stitch.Pick(s, tabindent.ModeAuto, tabindent.ModeTabs)))
			case 1:
				to = append(to, tabindent.WithLoad(stitch.Pick(s, tabindent.StyleAuto, tabindent.StyleSpaces, tabindent.StyleTabs)))
			case 2:
				to = append(to, tabindent.WithDump(stitch.Pick(s, tabindent.StyleAuto, tabindent.StyleSpaces, tabindent.StyleTabs)))
			default:
				to = append(to, tabindent.WithAuto(stitch.Pick(s, tabindent.ScopeDocument, tabindent.ScopeStream)))
			}
		}
		o.add(false, "tabindent", yaml.WithPlugin(tabindent.New(to...)))
	case 4:
		switch s.Choice(3) {
		case 0:
			o.add(true, "errfmt3", yaml.WithPlugin(errorformatv3.New()))
		case 1:
			o.add(true, "errfmt4", yaml.WithPlugin(errorformatv4.Must()))
		default:
			p, err := errorformatv4.New(errorformatv4.WithPositionStyle(stitch.Pick(s, errorformatv4.PositionShort, errorformatv4.PositionLong, errorformatv4.PositionLine, errorformatv4.PositionStyle(7))))
			if err != nil {
				return
			}
			o.add(true, "errfmt4pos", yaml.WithPlugin(p))
		}
	case 5:
		api := stitch.Pick(s, "loader-limits", "error-format", "merge-key", "alias-data", "tab-indent", "yaml-parser", "nope")
		o.add(api == "loader-limits" || api == "error-format", "named:"+api, yaml.WithNamedPlugin(api))
	default:
		o.add(true, "plugin(nil)", yaml.WithPlugin())
	}
}

func optsOf(s *stitch.Source, load bool) Opts {
	o := Opts{Plain: true}
	for s.More() && len(o.List) < 6 {
		if s.Choice(5) == 0 {
			// a nested group through Options()
			sub := Opts{Plain: true}
			for n := s.Choice(3) + 1; n > 0; n-- {
				if load {
					loadOpt(s, &sub)
				} else {
					formatOpt(s, &sub)
				}
			}
			o.Stream = o.Stream || sub.Stream
			o.add(sub.Plain, "options("+sub.String()+")", yaml.Options(sub.List...))
			continue
		}
		if load {
			loadOpt(s, &o)
		} else {
			formatOpt(s, &o)
		}
	}
	return o
}

// optsYAMLText: a configuration document for OptsYAML
func optsYAMLText(s *stitch.Source) string {
	var b, plugins strings.Builder
	for s.More() {
		switch s.Choice(9) {
		case 0:
			fmt.Fprintf(&b, "indent: %d\n", s.Range(-1, 10))
		case 1:
			fmt.Fprintf(&b, "%s: %s\n", stitch.Pick(s, "compact-seq-indent", "unicode", "canonical", "explicit-start", "explicit-end",
				"flow-simple-coll", "known-fields", "single-document", "unique-keys"), stitch.Pick(s, "true", "false", "1", "~"))
		case 2:
			fmt.Fprintf(&b, "line-width: %d\n", s.Range(-1, 100))
		case 3:
			fmt.Fprintf(&b, "line-break: %s\n", stitch.Pick(s, "ln", "cr", "crln", "lf", ""))
		case 4:
			fmt.Fprintf(&plugins, "  loader-limits:\n    depth: %s\n    alias: %s\n", stitch.Pick(s, "5", "100", "~", "x"), stitch.Pick(s, "5", "100", "~", "-1"))
		case 5:
			fmt.Fprintf(&plugins, "  error-format: %s\n", stitch.Pick(s, "v3", "v4", "{v4: {position: long}}", "{v4: {position: short}}", "v9", "{name: error-format, version: v1}"))
		case 6:
			fmt.Fprintf(&plugins, "  merge-key: %s\n", stitch.Pick(s, "true", "false", "{conflicts: last}", "{multiple: combine}", "{disable: true}", "~"))
		case 7:
			fmt.Fprintf(&plugins, "  %s: %s\n", stitch.Pick(s, "alias-data", "tab-indent", "yaml-parser"), stitch.Pick(s, "true", "{stream: true}", "{path: true}", "{mode: tabs}", "{data: {a: 1}}", "{}"))
		default:
			b.WriteString(s.Str(12))
		}
	}
	if plugins.Len() > 0 {
		b.WriteString("plugin:\n" + plugins.String())
	}
	return b.String()
}
