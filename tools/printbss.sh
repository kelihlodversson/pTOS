#!/bin/sh
# Determine the BSS usage of each subdirectory
# Written by Vincent Rivière, 2014-2019.
# License: Public domain

prefix=${CROSS_COMPILE-m68k-unknown-ptos-prgelf-}
total=0

for dir in aes bdos bios cli desk util vdi
do
  # Find all the .o corresponding to the sources of the directory (may exist or not)
  objects=$(find $dir -name '*.c' -or -name '*.S' |sed -n 's:[^/]*/\([^.]*\).*:obj/\1.o:p')

  # Get the total size of the .bss and COMMON sections
  dirsize=$("${prefix}size" -t --common $objects 2>/dev/null |grep '(TOTALS)' |awk '{print $3}')

  total=$(expr $total + $dirsize)

  printf "%-5s %7d\n" $dir $dirsize
done

echo -------------
printf "Total %7d\n" $total
