#!/bin/tcsh
source my_scripts/set_env.cshrc
python `which scons` -j 36 build/Garnet_standalone/gem5.debug
