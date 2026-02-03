#!/bin/bash

for d in */; do
	pushd "$d" >/dev/null;
	rm -f *.ll *.txt;
	popd >/dev/null;
done;


