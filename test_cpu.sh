#!/bin/bash

for f in roms/gb-test-roms/cpu_instrs/individual/*.gb; do
	echo "-------------------------------------------"
	echo ${f}
	./limeguy m9000000 "${f}"
done

