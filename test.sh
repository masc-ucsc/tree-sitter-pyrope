#!/bin/bash

for a in full_pyrope/*.prp; do
	./node_modules/tree-sitter-cli/tree-sitter parse -q $a
done
