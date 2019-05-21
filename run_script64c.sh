#!/bin/bash
#####	README
# .run_script.sh <num-cores> <routAlgo> <spin-freq> <rot> <uTurnSpinRing(3, 4)> <uTurnCrossbar(0, 1)>
###############################################################################
bench_caps=( 'UNIFORM_RANDOM' 'BIT_COMPLEMENT' 'BIT_REVERSE' 'BIT_ROTATION' 'TRANSPOSE' 'SHUFFLE' )
bench=( 'uniform_random' 'bit_complement' 'bit_reverse' 'bit_rotation' 'transpose' 'shuffle' )

routing_algorithm=( 'Adaptive_Random' )

d="04-18-2019"
out_dir="/usr/scratch/mayank/brownianNetwork_nocs2019_rslt/BBR/$d"
cycles=100000
vnet=0 #for multi-flit pkt: vnet = 2
tr=1
################# Give attention to the injection rate that you have got#############################
for b in 0 1 2 3 4 5
do
for vc_ in 2 4
do
for k in 0.02 0.04 0.06 0.08 0.10 0.12 0.14 0.16 0.18 0.20 0.22 0.24 0.26 0.28 0.30 0.32 0.34 0.36 0.38 0.40 0.42 0.44 0.46 0.48 0.50
do
	./build/Garnet_standalone/gem5.opt -d $out_dir/64c/regular/${routing_algorithm[0]}/${bench_caps[$b]}/vc-${vc_}/inj-${k} configs/example/garnet_synth_traffic.py --topology=Mesh_XY --num-cpus=64 --num-dirs=64 --mesh-rows=8 --network=garnet2.0 --router-latency=1 --sim-cycles=${cycles} --inj-vnet=0 --vcs-per-vnet=${vc_} --sim-type=2  --swizzle-swap=1 --policy=1 --tdm=1 --injectionrate=${k} --synthetic=${bench[$b]} --routing-algorithm=3 &
done
#sleep 300
done
done