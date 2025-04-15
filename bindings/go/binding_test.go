package tree_sitter_pyrope_test

import (
	"testing"

	tree_sitter "github.com/tree-sitter/go-tree-sitter"
	tree_sitter_pyrope "github.com/masc-ucsc/tree-sitter-pyrope/bindings/go"
)

func TestCanLoadGrammar(t *testing.T) {
	language := tree_sitter.NewLanguage(tree_sitter_pyrope.Language())
	if language == nil {
		t.Errorf("Error loading Pyrope grammar")
	}
}
