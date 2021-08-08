#!/bin/bash
#####	README
# .run_script.sh <num-cores> <routAlgo> <spin-freq> <rot> <uTurnSpinRing(3, 4)> <uTurnCrossbar(0, 1)>
###############################################################################
bench_caps=( 'UNIFORM_RANDOM' 'BIT_COMPLEMENT' 'BIT_REVERSE' 'BIT_ROTATION' 'TRANSPOSE' 'SHUFFLE' )
bench=( 'uniform_random' 'bit_complement' 'bit_reverse' 'bit_rotation' 'transpose' 'shuffle' )

routing_algorithm=( 'Adaptive_Random' )

d="05-21-2019"
out_dir="/usr/scratch/mayank/brownianNetwork_nocs2019_rslt/BBR/$d"
cycles=100000
vnet=0 #for multi-flit pkt: vnet = 2
tr=1
################# Give attention to the injection rate that you have got#############################
for b in 0 4
do
for vc_ in 2 4
do
for k in 0.02 0.04 0.06 0.08 0.10 0.12 0.14 0.16 0.18 0.20 0.22 0.24 0.26 0.28 0.30 0.32 0.34 0.36 0.38 0.40 0.42 0.44 0.46 0.48 0.50 0.52 0.54 0.56 0.58 0.60 0.62	0.64 0.66 0.68 0.70 0.72 0.74 0.76 0.78 0.80 0.82 0.84 0.86 0.88 0.90 0.92 0.94 0.96 0.98 1.00
do
	./build/Garnet_standalone/gem5.opt -d $out_dir/64c/Torus/${routing_algorithm[0]}/${bench_caps[$b]}/vc-${vc_}/inj-${k} configs/example/garnet_synth_traffic.py --topology=Torus --num-cpus=64 --num-dirs=64 --mesh-rows=8 --network=garnet2.0 --router-latency=1 --sim-cycles=${cycles} --inj-vnet=0 --vcs-per-vnet=${vc_} --sim-type=1  --swizzle-swap=1 --policy=1 --tdm=1 --injectionrate=${k} --synthetic=${bench[$b]} --routing-algorithm=0 &
done
#sleep 300
done
done