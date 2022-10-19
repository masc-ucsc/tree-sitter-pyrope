#!/bin/bash

mkdir -p benchtest
rm -f benchtest/large1.prp
for a in snippets/docs.prp
do
  echo "// ${a}" >>benchtest/large1.prp
  cat $a         >>benchtest/large1.prp
done

cat benchtest/large1.prp benchtest/large1.prp benchtest/large1.prp benchtest/large1.prp >benchtest/large2.prp
cat benchtest/large2.prp benchtest/large2.prp benchtest/large2.prp benchtest/large2.prp >benchtest/large3.prp
cat benchtest/large3.prp benchtest/large3.prp benchtest/large3.prp benchtest/large3.prp >benchtest/large4.prp
cat benchtest/large4.prp benchtest/large4.prp benchtest/large4.prp benchtest/large3.prp >benchtest/large5.prp
#cat benchtest/large5.prp benchtest/large5.prp benchtest/large5.prp benchtest/large5.prp >benchtest/large6.prp

for a in benchtest/large*;
do
  ./node_modules/tree-sitter-cli/tree-sitter parse -q -t $a;
done

