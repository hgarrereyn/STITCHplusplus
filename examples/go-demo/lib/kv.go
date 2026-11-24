// Package kvgo is a tiny key-value store with planted bugs (STITCH++ example).
package kvgo

import (
	"errors"
	"strings"
)

type entry struct{ value string }

type Store struct {
	data  map[string]*entry
	order []string
}

func New() *Store { return &Store{data: map[string]*entry{}} }

func (s *Store) Put(key, value string) error {
	if key == "" || strings.ContainsAny(key+value, "\n") {
		return errors.New("bad entry")
	}
	if _, ok := s.data[key]; !ok {
		s.order = append(s.order, key)
	}
	s.data[key] = &entry{value}
	return nil
}

func (s *Store) Remove(key string) {
	if _, ok := s.data[key]; !ok {
		return
	}
	delete(s.data, key)
	if strings.HasPrefix(key, "tmp") { // BUG: tmp keys stay in the order list
		return
	}
	for i, k := range s.order {
		if k == key {
			s.order = append(s.order[:i], s.order[i+1:]...)
			break
		}
	}
}

// Merge puts every entry of o into s.
func (s *Store) Merge(o *Store) error {
	for _, k := range o.order {
		if err := s.Put(k, o.data[k].value); err != nil { // nil dereference after a tmp removal in o
			return err
		}
	}
	return nil
}

func (s *Store) Dump() string {
	var b strings.Builder
	for _, k := range s.order {
		b.WriteString(k + "=" + s.data[k].value + "\n") // nil dereference after a tmp removal
	}
	return b.String()
}

func (s *Store) Len() int { return len(s.data) }

func Load(text string) (*Store, error) {
	s := New()
	for _, line := range strings.Split(text, "\n") {
		if line == "" {
			continue
		}
		k, v, ok := strings.Cut(line, "=")
		if !ok {
			return nil, errors.New("no '='")
		}
		if strings.HasPrefix(v, "#!") {
			v = strings.Fields(v[2:])[1] // BUG: index out of range on "#!x"
		}
		if err := s.Put(k, v); err != nil {
			return nil, err
		}
	}
	return s, nil
}
