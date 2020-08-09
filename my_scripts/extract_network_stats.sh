echo > network_stats.txt
grep "packets_injected::total" m5out/stats.txt | sed 's/system.ruby.network.packets_injected::total\s*/packets_injected = /' >> network_stats.txt
grep "packets_received::total" m5out/stats.txt | sed 's/system.ruby.network.packets_received::total\s*/packets_received = /' >> network_stats.txt
grep "average_packet_queueing_latency" m5out/stats.txt | sed 's/system.ruby.network.average_packet_queueing_latency\s*/average_packet_queueing_latency = /' >> network_stats.txt
grep "average_packet_network_latency" m5out/stats.txt | sed 's/system.ruby.network.average_packet_network_latency\s*/average_packet_network_latency = /' >> network_stats.txt
grep "average_packet_latency" m5out/stats.txt | sed 's/system.ruby.network.average_packet_latency\s*/average_packet_latency = /' >> network_stats.txt
grep "flits_injected::total" m5out/stats.txt | sed 's/system.ruby.network.flits_injected::total\s*/flits_injected = /' >> network_stats.txt
grep "flits_received::total" m5out/stats.txt | sed 's/system.ruby.network.flits_received::total\s*/flits_received = /' >> network_stats.txt
grep "average_flit_queueing_latency" m5out/stats.txt | sed 's/system.ruby.network.average_flit_queueing_latency\s*/average_flit_queueing_latency = /' >> network_stats.txt
grep "average_flit_network_latency" m5out/stats.txt | sed 's/system.ruby.network.average_flit_network_latency\s*/average_flit_network_latency = /' >> network_stats.txt
grep "average_flit_latency" m5out/stats.txt | sed 's/system.ruby.network.average_flit_latency\s*/average_flit_latency = /' >> network_stats.txt
grep "average_hops" m5out/stats.txt | sed 's/system.ruby.network.average_hops\s*/average_hops = /' >> network_stats.txt
grep "bubble_swizzles" m5out/stats.txt | sed 's/system.ruby.network.bubble_swizzles\s*/bubble_swizzles = /' >> network_stats.txt
grep "\.bubble_swaps" m5out/stats.txt | sed 's/system.ruby.network.bubble_swaps\s*/bubble_swaps = /' >> network_stats.txt
grep ".routed_bubble_swaps" m5out/stats.txt | sed 's/system.ruby.network.routed_bubble_swaps\s*/routed_bubble_swaps = /' >> network_stats.txt
grep "total_bailout" m5out/stats.txt | sed 's/system.ruby.network.total_bailout\s*/bailouts = /' >> network_stats.txt
