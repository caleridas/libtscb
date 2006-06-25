#!/bin/sh

make maintainerclean
autoconf2.50
./configure
make tests && make distclean && tla tree-lint
