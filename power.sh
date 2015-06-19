#! /bin/bash

FILES="vtr_flow/benchmarks/verilog/ch_intrinsics.v"

for file in $FILES
do 
	vtr_flow/scripts/run_vtr_flow_power.pl vpr/sample_arch_StratixIV.xml $file HPower/power_lib_synopsys_32nm_TT_mux4.xml -vpr_route_chan_width 300
	filename=$(basename $file .v)
	echo -e "Output result to temp/$filename.power"
done

