#!/bin/sh
for ((i = 0; i < $1; i++));
	do cat $2.txt >> $2_$1.txt;
done;
