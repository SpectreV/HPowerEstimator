#! /bin/bash

FILES="vtr_flow/benchmarks/blif/alu4.blif"

for file in $FILES
do 
	filename=$(basename $file .blif)
	echo $filename
	ace2/ace -b $file -n temp/$filename.blif -o temp/$filename.act
	vpr/vpr vpr/sample_arch_StratixIV.xml temp/$filename -route_file temp/$filename.route -place_file temp/$filename.place -net_file temp/$filename.net -route_chan_width 300 -nodisp
	HPower/hpower vpr/sample_arch_StratixIV.xml temp/$filename HPower/power_lib1.xml
	echo -e "\n%%%%%%%%Benchmark $filename power simulation ended%%%%%%%%%\n"
done
