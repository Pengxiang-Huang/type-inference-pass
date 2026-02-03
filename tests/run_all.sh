#!/bin/bash

for d in */; do
	pushd "$d" >/dev/null;
	make ;
	popd >/dev/null;
done
