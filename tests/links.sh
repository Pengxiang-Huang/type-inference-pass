#! /bin/bash

for d in */; do
	pushd "$d" >/dev/null;
	if [[ ! -f "Makefile" ]] ; then 
		ln -s ../Makefile Makefile 
	fi
	popd >/dev/null;
done
