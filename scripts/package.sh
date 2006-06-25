#!/bin/bash

make maintainerclean

VERSION=0.1
TMPDIR=/tmp/libtscb-$VERSION

mkdir $TMPDIR
cp -R . $TMPDIR

(cd $TMPDIR; autoconf2.50; rm -rf \{arch\} autom4te.cache)
(cd $TMPDIR; find -name ".arch-ids" | xargs rm -rf)

(cd /tmp; tar czf libtscb.tar.gz --owner=root --group=root libtscb-$VERSION)
