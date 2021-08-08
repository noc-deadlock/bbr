/*
 * Copyright (c) 2008 Princeton University
 * Copyright (c) 2016 Georgia Institute of Technology
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Authors: Niket Agarwal
 *          Tushar Krishna
 */


#include "mem/ruby/network/garnet2.0/GarnetNetwork.hh"

#include <cassert>

#include "base/cast.hh"
#include "base/stl_helpers.hh"
#include "mem/ruby/common/NetDest.hh"
#include "mem/ruby/network/MessageBuffer.hh"
#include "mem/ruby/network/garnet2.0/CommonTypes.hh"
#include "mem/ruby/network/garnet2.0/CreditLink.hh"
#include "mem/ruby/network/garnet2.0/GarnetLink.hh"
#include "mem/ruby/network/garnet2.0/NetworkInterface.hh"
#include "mem/ruby/network/garnet2.0/NetworkLink.hh"
#include "mem/ruby/network/garnet2.0/Router.hh"
#include "mem/ruby/network/garnet2.0/RoutingUnit.hh"
#include "mem/ruby/network/garnet2.0/InputUnit.hh"
#include "mem/ruby/system/RubySystem.hh"

using namespace std;
using m5::stl_helpers::deletePointers;

/*
 * GarnetNetwork sets up the routers and links and collects stats.
 * Default parameters (GarnetNetwork.py) can be overwritten from command line
 * (see configs/network/Network.py)
 */

GarnetNetwork::GarnetNetwork(const Params *p)
    : Network(p)
{
    m_num_rows = p->num_rows;
    m_ni_flit_size = p->ni_flit_size;
    m_vcs_per_vnet = p->vcs_per_vnet;
    m_buffers_per_data_vc = p->buffers_per_data_vc;
    m_buffers_per_ctrl_vc = p->buffers_per_ctrl_vc;
    m_routing_algorithm = p->routing_algorithm;

    warmup_cycles = p->warmup_cycles;
    marked_flits = p->marked_flits;
    marked_flt_injected = 0;
    marked_flt_received = 0;
    marked_pkt_received = 0;
    marked_pkt_injected = 0;
    total_marked_flit_latency = 0;
    total_marked_flit_received = 0;
    flit_latency = Cycles(0);
    flit_network_latency = Cycles(0);
    flit_queueing_latency = Cycles(0);
    marked_flit_latency = Cycles(0);
    marked_flit_network_latency = Cycles(0);
    marked_flit_queueing_latency = Cycles(0);
    sim_type = p->sim_type;
    cout << "sim-type: " << sim_type << endl;

    tdm_ = p->tdm;
    m_swizzleSwap = p->swizzle_swap;
    m_policy = p->policy;
    prnt_cycle = 800;

    if (m_swizzleSwap) {
        // If interswap is set then 'whenToSwap' and 'whichToSwap' should
        // not be equal to 0. Assert.
    #if (MY_PRINT)
            cout << "***********************************" << endl;
            cout << "swizzleSwap is enabled" << endl;
            cout << "***********************************" << endl;
    #endif
        if (m_policy == MINIMAL_) {
        #if (MY_PRINT)
                cout << "***********************************" << endl;
                cout << " 'policy' :::: MINIMAL swizzleSwap Policy is used" << endl;
                cout << "***********************************" << endl;
        #endif
        } else if (m_policy == NON_MINIMAL_) {
        #if (MY_PRINT)
                cout << "***********************************" << endl;
                cout << " 'policy' :::: " << endl;
                cout << "'policy' :::: NON_MINIMAL swizzleSwap Policy is used"\
                     << endl;
                cout << "***********************************" << endl;
        #endif
        }

    }

    m_enable_fault_model = p->enable_fault_model;
    if (m_enable_fault_model)
        fault_model = p->fault_model;

    m_vnet_type.resize(m_virtual_networks);

    for (int i = 0 ; i < m_virtual_networks ; i++) {
        if (m_vnet_type_names[i] == "response")
            m_vnet_type[i] = DATA_VNET_; // carries data (and ctrl) packets
        else
            m_vnet_type[i] = CTRL_VNET_; // carries only ctrl packets
    }

    // record the routers
    for (vector<BasicRouter*>::const_iterator i =  p->routers.begin();
         i != p->routers.end(); ++i) {
        Router* router = safe_cast<Router*>(*i);
        m_routers.push_back(router);

        // initialize the router's network pointers
        router->init_net_ptr(this);
    }

    // record the network interfaces
    for (vector<ClockedObject*>::const_iterator i = p->netifs.begin();
         i != p->netifs.end(); ++i) {
        NetworkInterface *ni = safe_cast<NetworkInterface *>(*i);
        m_nis.push_back(ni);
        ni->init_net_ptr(this);
    }
}

void
GarnetNetwork::init()
{
    Network::init();

    for (int i=0; i < m_nodes; i++) {
        m_nis[i]->addNode(m_toNetQueues[i], m_fromNetQueues[i]);
    }

    // The topology pointer should have already been initialized in the
    // parent network constructor
    assert(m_topology_ptr != NULL);
    m_topology_ptr->createLinks(this);

    // Initialize topology specific parameters
    if (getNumRows() > 0) {
        // Only for Mesh topology
        // m_num_rows and m_num_cols are only used for
        // implementing XY or custom routing in RoutingUnit.cc
        m_num_rows = getNumRows();
        m_num_cols = m_routers.size() / m_num_rows;
        assert(m_num_rows * m_num_cols == m_routers.size());
    } else {
        m_num_rows = -1;
        m_num_cols = -1;
    }

    // FaultModel: declare each router to the fault model
    if (isFaultModelEnabled()) {
        for (vector<Router*>::const_iterator i= m_routers.begin();
             i != m_routers.end(); ++i) {
            Router* router = safe_cast<Router*>(*i);
            int router_id M5_VAR_USED =
                fault_model->declare_router(router->get_num_inports(),
                                            router->get_num_outports(),
                                            router->get_vc_per_vnet(),
                                            getBuffersPerDataVC(),
                                            getBuffersPerCtrlVC());
            assert(router_id == router->get_id());
            router->printAggregateFaultProbability(cout);
            router->printFaultVector(cout);
        }
    }
}

GarnetNetwork::~GarnetNetwork()
{
    deletePointers(m_routers);
    deletePointers(m_nis);
    deletePointers(m_networklinks);
    deletePointers(m_creditlinks);
}

/*
 * This function creates a link from the Network Interface (NI)
 * into the Network.
 * It creates a Network Link from the NI to a Router and a Credit Link from
 * the Router to the NI
*/

void
GarnetNetwork::makeExtInLink(NodeID src, SwitchID dest, BasicLink* link,
                            const NetDest& routing_table_entry)
{
    assert(src < m_nodes);

    GarnetExtLink* garnet_link = safe_cast<GarnetExtLink*>(link);

    // GarnetExtLink is bi-directional
    NetworkLink* net_link = garnet_link->m_network_links[LinkDirection_In];
    net_link->setType(EXT_IN_);
    CreditLink* credit_link = garnet_link->m_credit_links[LinkDirection_In];

    m_networklinks.push_back(net_link);
    m_creditlinks.push_back(credit_link);

    PortDirection dst_inport_dirn = "Local";
    m_routers[dest]->addInPort(dst_inport_dirn, net_link, credit_link);
    m_nis[src]->addOutPort(net_link, credit_link, dest);
}

/*
 * This function creates a link from the Network to a NI.
 * It creates a Network Link from a Router to the NI and
 * a Credit Link from NI to the Router
*/

void
GarnetNetwork::makeExtOutLink(SwitchID src, NodeID dest, BasicLink* link,
                             const NetDest& routing_table_entry)
{
    assert(dest < m_nodes);
    assert(src < m_routers.size());
    assert(m_routers[src] != NULL);

    GarnetExtLink* garnet_link = safe_cast<GarnetExtLink*>(link);

    // GarnetExtLink is bi-directional
    NetworkLink* net_link = garnet_link->m_network_links[LinkDirection_Out];
    net_link->setType(EXT_OUT_);
    CreditLink* credit_link = garnet_link->m_credit_links[LinkDirection_Out];

    m_networklinks.push_back(net_link);
    m_creditlinks.push_back(credit_link);

    PortDirection src_outport_dirn = "Local";
    m_routers[src]->addOutPort(src_outport_dirn, net_link,
                               routing_table_entry,
                               link->m_weight, credit_link);
    m_nis[dest]->addInPort(net_link, credit_link);
}

/*
 * This function creates an internal network link between two routers.
 * It adds both the network link and an opposite credit link.
*/

void
GarnetNetwork::makeInternalLink(SwitchID src, SwitchID dest, BasicLink* link,
                                const NetDest& routing_table_entry,
                                PortDirection src_outport_dirn,
                                PortDirection dst_inport_dirn)
{
    GarnetIntLink* garnet_link = safe_cast<GarnetIntLink*>(link);

    // GarnetIntLink is unidirectional
    NetworkLink* net_link = garnet_link->m_network_link;
    net_link->setType(INT_);
    CreditLink* credit_link = garnet_link->m_credit_link;

    m_networklinks.push_back(net_link);
    m_creditlinks.push_back(credit_link);

    m_routers[dest]->addInPort(dst_inport_dirn, net_link, credit_link);
    m_routers[src]->addOutPort(src_outport_dirn, net_link,
                               routing_table_entry,
                               link->m_weight, credit_link);
}

// Total routers in the network
int
GarnetNetwork::getNumRouters()
{
    return m_routers.size();
}

// Get ID of router connected to a NI.
int
GarnetNetwork::get_router_id(int ni)
{
    return m_nis[ni]->get_router_id();
}

Router*
GarnetNetwork::get_RouterInDirn( PortDirection outport_dir, int my_id )
{
    int num_cols = getNumCols();
    int downstream_id = -1; // router_id for downstream router
//    cout << "GarnetNetwork::get_RouterInDirn: outport_dir: " << outport_dir << endl;
//    cout << "GarnetNetwork::get_RouterInDirn: my_id: " << my_id << endl;
    // Do border control check here...

    /*outport direction from the flit for this router*/
    if (outport_dir == "East") {
        if ( my_id % num_cols == (num_cols - 1) )
            downstream_id = my_id - num_cols + 1;
        else
            downstream_id = my_id + 1;
        // constraints on downstream router-id
        for(int k=(num_cols-1); k < getNumRouters(); k+=num_cols ) {
            // assert(my_id != k); // In Torus topology this does not hold True
        }
    }
    else if (outport_dir == "West") {
        if ( my_id % num_cols == 0 )
            downstream_id = my_id + num_cols - 1;
        else
            downstream_id = my_id - 1;
        // constraints on downstream router-id
        for(int k=0; k < getNumRouters(); k+=num_cols ) {
            // assert(my_id != k); // In Torus topology this does not hold True
        }
    }
    else if (outport_dir == "North") {
        if ( my_id / num_cols == (num_cols - 1) )
            downstream_id = my_id % num_cols;
        else
            downstream_id = my_id + num_cols;
        // constraints on downstream router-id
        for(int k=((num_cols-1)*num_cols); k < getNumRouters(); k+=1 ) {
            // assert(my_id != k); // In Torus topology this does not hold True
        }
    }
    else if (outport_dir == "South") {
        if ( my_id / num_cols == 0 )
            downstream_id = my_id + (num_cols)*(num_cols - 1);
        else
            downstream_id = my_id - num_cols;
        // constraints on downstream router-id
        for(int k=0; k < num_cols; k+=1 ) {
            // assert(my_id != k); // In Torus topology this does not hold True
        }
    }
    else if (outport_dir == "Local"){
        #if (MY_PRINT)
            cout << "outport_dir: " << outport_dir << endl;
        #endif
        assert(0);
        return NULL;
    }
    else {
        #if (MY_PRINT)
            cout << "outport_dir: " << outport_dir << endl;
        #endif
        assert(0); // for completion of if-else chain
        return NULL;
    }

//    cout << "GarnetNetwork::get_RouterInDirn: downstream_id: " << downstream_id << endl;
//    cout << "GarnetNetwork::get_RouterInDirn: my_id: " << my_id << endl;
//    cout << "----------------" << endl;

//    if ((downstream_id < 0) || (downstream_id >= getNumRouters())) {
//        assert(0);
//        return NULL;
//    } else
    assert(downstream_id >= 0);
    return m_routers[downstream_id];
//    return downstream_id;
}


// scanNetwork function to loop through all routers
// and print their states.
void
GarnetNetwork::scanNetwork()
{
    cout << "**********************************************" << endl;
    for (vector<Router*>::const_iterator itr= m_routers.begin();
         itr != m_routers.end(); ++itr) {
        Router* router = safe_cast<Router*>(*itr);
        cout << "--------" << endl;
        cout << "Router_id: " << router->get_id() << endl;;

        cout << "~~~~~~~~~~~~~~~" << endl;
        for (int inport = 0; inport < router->get_num_inports(); inport++) {
            // print here the inport ID and flit in that inport...
            cout << "inport: " << inport << " direction: " << router->get_inputUnit_ref()[inport]\
                                                                    ->get_direction() << endl;
            assert(inport == router->get_inputUnit_ref()[inport]->get_id());
            if(router->get_inputUnit_ref()[inport]->vc_isEmpty(0)) {
                if(router->critical_inport.id == inport)
                    cout << "inport is empty and critical" << endl;
                else
                    cout << "inport is empty" << endl;
            } else {
//                cout << "flit info in this inport:" << endl;
                cout << *(router->get_inputUnit_ref()[inport]->peekTopFlit(0)) << endl;
            }
        }

    }
    cout << "**********************************************" << endl;
    return;
}


void
GarnetNetwork::regStats()
{
    Network::regStats();

    m_pre_mature_exit
        .name(name() + ".pre_mature_exit");

    m_marked_flt_dist
        .init(m_routers.size())
        .name(name() + ".marked_flit_distribution")
        .flags(Stats::pdf | Stats::total | Stats::nozero | Stats::oneline)
        ;

    m_flt_dist
        .init(m_routers.size())
        .name(name() + ".flit_distribution")
        .flags(Stats::pdf | Stats::total | Stats::nozero | Stats::oneline)
        ;


    m_network_latency_histogram
        .init(21)
        .name(name() + ".network_latency_histogram")
        .flags(Stats::pdf | Stats::total | Stats::nozero | Stats::oneline)
        ;

    m_flt_latency_hist
        .init(100)
        .name(name() + ".flit_latency_histogram")
        .flags(Stats::pdf | Stats::total | Stats::nozero | Stats::oneline)
        ;

    m_flt_network_latency_hist
        .init(100)
        .name(name() + ".flit_network_latency_histogram")
        .flags(Stats::pdf | Stats::total | Stats::nozero | Stats::oneline)
        ;

    m_flt_queueing_latency_hist
        .init(100)
        .name(name() + ".flit_queueing_latency_histogram")
        .flags(Stats::pdf | Stats::total | Stats::nozero | Stats::oneline)
        ;

    m_marked_flt_latency_hist
        .init(100)
        .name(name() + ".marked_flit_latency_histogram")
        .flags(Stats::pdf | Stats::total | Stats::nozero | Stats::oneline)
        ;


    m_marked_flt_network_latency_hist
        .init(100)
        .name(name() + ".marked_flit_network_latency_histogram")
        .flags(Stats::pdf | Stats::total | Stats::nozero | Stats::oneline)
        ;

    m_marked_flt_queueing_latency_hist
        .init(100)
        .name(name() + ".marked_flit_queueing_latency_histogram")
        .flags(Stats::pdf | Stats::total | Stats::nozero | Stats::oneline)
        ;



    // Packets
    m_packets_received
        .init(m_virtual_networks)
        .name(name() + ".packets_received")
        .flags(Stats::pdf | Stats::total | Stats::nozero | Stats::oneline)
        ;

    m_packets_injected
        .init(m_virtual_networks)
        .name(name() + ".packets_injected")
        .flags(Stats::pdf | Stats::total | Stats::nozero | Stats::oneline)
        ;

    m_packet_network_latency
        .init(m_virtual_networks)
        .name(name() + ".packet_network_latency")
        .flags(Stats::oneline)
        ;

    m_packet_queueing_latency
        .init(m_virtual_networks)
        .name(name() + ".packet_queueing_latency")
        .flags(Stats::oneline)
        ;

    m_max_flit_latency
        .name(name() + ".max_flit_latency");
    m_max_flit_network_latency
        .name(name() + ".max_flit_network_latency");
    m_max_flit_queueing_latency
        .name(name() + ".max_flit_queueing_latency");
    m_max_marked_flit_latency
        .name(name() + ".max_marked_flit_latency");
    m_max_marked_flit_network_latency
        .name(name() + ".max_marked_flit_network_latency");
    m_max_marked_flit_queueing_latency
        .name(name() + ".max_marked_flit_queueing_latency");

    m_marked_pkt_received
        .init(m_virtual_networks)
        .name(name() + ".marked_pkt_receivced")
        .flags(Stats::pdf | Stats::total | Stats::nozero | Stats::oneline)
        ;
    m_marked_pkt_injected
        .init(m_virtual_networks)
        .name(name() + ".marked_pkt_injected")
        .flags(Stats::pdf | Stats::total | Stats::nozero | Stats::oneline)
        ;
    m_marked_pkt_network_latency
        .init(m_virtual_networks)
        .name(name() + ".marked_pkt_network_latency")
        .flags(Stats::pdf | Stats::total | Stats::nozero | Stats::oneline)
        ;
    m_marked_pkt_queueing_latency
        .init(m_virtual_networks)
        .name(name() + ".marked_pkt_queueing_latency")
        .flags(Stats::pdf | Stats::total | Stats::nozero | Stats::oneline)
        ;


    for (int i = 0; i < m_virtual_networks; i++) {
        m_packets_received.subname(i, csprintf("vnet-%i", i));
        m_packets_injected.subname(i, csprintf("vnet-%i", i));
        m_marked_pkt_injected.subname(i, csprintf("vnet-%i", i));
        m_marked_pkt_received.subname(i, csprintf("vnet-%i", i));
        m_packet_network_latency.subname(i, csprintf("vnet-%i", i));
        m_packet_queueing_latency.subname(i, csprintf("vnet-%i", i));
        m_marked_pkt_network_latency.subname(i, csprintf("vnet-%i", i));
        m_marked_pkt_queueing_latency.subname(i, csprintf("vnet-%i", i));
    }

    m_avg_packet_vnet_latency
        .name(name() + ".average_packet_vnet_latency")
        .flags(Stats::oneline);
    m_avg_packet_vnet_latency =
        m_packet_network_latency / m_packets_received;

    m_avg_packet_vqueue_latency
        .name(name() + ".average_packet_vqueue_latency")
        .flags(Stats::oneline);
    m_avg_packet_vqueue_latency =
        m_packet_queueing_latency / m_packets_received;

    m_avg_packet_network_latency
        .name(name() + ".average_packet_network_latency");
    m_avg_packet_network_latency =
        sum(m_packet_network_latency) / sum(m_packets_received);

    m_avg_packet_queueing_latency
        .name(name() + ".average_packet_queueing_latency");
    m_avg_packet_queueing_latency
        = sum(m_packet_queueing_latency) / sum(m_packets_received);

    m_avg_packet_latency
        .name(name() + ".average_packet_latency");
    m_avg_packet_latency
        = m_avg_packet_network_latency + m_avg_packet_queueing_latency;

    m_avg_marked_pkt_network_latency
        .name(name() + ".average_marked_pkt_network_latency");
    m_avg_marked_pkt_network_latency =
        sum(m_marked_pkt_network_latency) / sum(m_marked_pkt_received);

    m_avg_marked_pkt_queueing_latency
        .name(name() + ".average_marked_pkt_queueing_latency");
    m_avg_marked_pkt_queueing_latency =
        sum(m_marked_pkt_queueing_latency) / sum(m_marked_pkt_received);

    m_avg_marked_pkt_latency
        .name(name() + ".average_marked_pkt_latency");
    m_avg_marked_pkt_latency
        = m_avg_marked_pkt_network_latency + m_avg_marked_pkt_queueing_latency;

    // Flits
    m_flits_received
        .init(m_virtual_networks)
        .name(name() + ".flits_received")
        .flags(Stats::pdf | Stats::total | Stats::nozero | Stats::oneline)
        ;

    m_flits_injected
        .init(m_virtual_networks)
        .name(name() + ".flits_injected")
        .flags(Stats::pdf | Stats::total | Stats::nozero | Stats::oneline)
        ;

    m_flit_network_latency
        .init(m_virtual_networks)
        .name(name() + ".flit_network_latency")
        .flags(Stats::oneline)
        ;

    m_flit_queueing_latency
        .init(m_virtual_networks)
        .name(name() + ".flit_queueing_latency")
        .flags(Stats::oneline)
        ;

    m_marked_flt_injected
        .init(m_virtual_networks)
        .name(name() + ".marked_flt_injected")
        .flags(Stats::pdf | Stats::total | Stats::nozero | Stats::oneline)
        ;
    m_marked_flt_received
        .init(m_virtual_networks)
        .name(name() + ".marked_flt_received")
        .flags(Stats::pdf | Stats::total | Stats::nozero | Stats::oneline)
        ;
    m_marked_flt_network_latency
        .init(m_virtual_networks)
        .name(name() + ".marked_flt_network_latency")
        .flags(Stats::pdf | Stats::total | Stats::nozero | Stats::oneline)
        ;
    m_marked_flt_queueing_latency
        .init(m_virtual_networks)
        .name(name() + ".marked_flt_queueing_latency")
        .flags(Stats::pdf | Stats::total | Stats::nozero | Stats::oneline)
        ;

    for (int i = 0; i < m_virtual_networks; i++) {
        m_flits_received.subname(i, csprintf("vnet-%i", i));
        m_flits_injected.subname(i, csprintf("vnet-%i", i));
        m_marked_flt_received.subname(i, csprintf("vnet-%i", i));
        m_marked_flt_injected.subname(i, csprintf("vnet-%i", i));
        m_flit_network_latency.subname(i, csprintf("vnet-%i", i));
        m_flit_queueing_latency.subname(i, csprintf("vnet-%i", i));
        m_marked_flt_network_latency.subname(i, csprintf("vnet-%i", i));
        m_marked_flt_queueing_latency.subname(i, csprintf("vnet-%i", i));
    }

    m_avg_flit_vnet_latency
        .name(name() + ".average_flit_vnet_latency")
        .flags(Stats::oneline);
    m_avg_flit_vnet_latency = m_flit_network_latency / m_flits_received;

    m_avg_flit_vqueue_latency
        .name(name() + ".average_flit_vqueue_latency")
        .flags(Stats::oneline);
    m_avg_flit_vqueue_latency =
        m_flit_queueing_latency / m_flits_received;

    m_avg_flit_network_latency
        .name(name() + ".average_flit_network_latency");
    m_avg_flit_network_latency =
        sum(m_flit_network_latency) / sum(m_flits_received);

    m_avg_flit_queueing_latency
        .name(name() + ".average_flit_queueing_latency");
    m_avg_flit_queueing_latency =
        sum(m_flit_queueing_latency) / sum(m_flits_received);

    m_avg_flit_latency
        .name(name() + ".average_flit_latency");
    m_avg_flit_latency =
        m_avg_flit_network_latency + m_avg_flit_queueing_latency;

    m_avg_marked_flt_network_latency
        .name(name() + ".average_marked_flt_network_latency");
    m_avg_marked_flt_network_latency =
        sum(m_marked_flt_network_latency) / sum(m_marked_flt_received);

    m_avg_marked_flt_queueing_latency
        .name(name() + ".average_marked_flt_queueing_latency");
    m_avg_marked_flt_queueing_latency =
        sum(m_marked_flt_queueing_latency) / sum(m_marked_flt_received);

    m_avg_marked_flt_latency
        .name(name() + ".average_marked_flt_latency");
    m_avg_marked_flt_latency
        = m_avg_marked_flt_network_latency + m_avg_marked_flt_queueing_latency;

    // Hops
    m_avg_hops.name(name() + ".average_hops");
    m_avg_hops = m_total_hops / sum(m_flits_received);

    m_marked_avg_hops.name(name() + ".marked_average_hops");
    m_marked_avg_hops = m_marked_total_hops / sum(m_marked_flt_received);

    // Links
    m_total_ext_in_link_utilization
        .name(name() + ".ext_in_link_utilization");
    m_total_ext_out_link_utilization
        .name(name() + ".ext_out_link_utilization");
    m_total_int_link_utilization
        .name(name() + ".int_link_utilization");
    m_average_link_utilization
        .name(name() + ".avg_link_utilization");

    num_bubbleSwizzles
        .name(name() + ".bubble_swizzles");
    num_bubbleSwaps
        .name(name() + ".bubble_swaps");
    num_routed_bubbleSwaps
        .name(name() + ".routed_bubble_swaps");

    m_average_vc_load
        .init(m_virtual_networks * m_vcs_per_vnet)
        .name(name() + ".avg_vc_load")
        .flags(Stats::pdf | Stats::total | Stats::nozero | Stats::oneline)
        ;
}

void
GarnetNetwork::collateStats()
{
    RubySystem *rs = params()->ruby_system;
    double time_delta = double(curCycle() - rs->getStartCycle());

    for (int i = 0; i < m_networklinks.size(); i++) {
        link_type type = m_networklinks[i]->getType();
        int activity = m_networklinks[i]->getLinkUtilization();

        if (type == EXT_IN_)
            m_total_ext_in_link_utilization += activity;
        else if (type == EXT_OUT_)
            m_total_ext_out_link_utilization += activity;
        else if (type == INT_)
            m_total_int_link_utilization += activity;

        m_average_link_utilization +=
            (double(activity) / time_delta);

        vector<unsigned int> vc_load = m_networklinks[i]->getVcLoad();
        for (int j = 0; j < vc_load.size(); j++) {
            m_average_vc_load[j] += ((double)vc_load[j] / time_delta);
        }
    }

    // Ask the routers to collate their statistics
    for (int i = 0; i < m_routers.size(); i++) {
        m_routers[i]->collateStats();
    }
}

bool
GarnetNetwork::check_mrkd_flt()
{
    int itr = 0;
    for(itr = 0; itr < m_routers.size(); ++itr) {
      if(m_routers.at(itr)->mrkd_flt_ > 0)
          break;
    }

    if(itr < m_routers.size()) {
      return false;
    }
    else {
        if(marked_flt_received < marked_flt_injected )
            return false;
        else
            return true;
    }
}


void
GarnetNetwork::print(ostream& out) const
{
    out << "[GarnetNetwork]";
}

GarnetNetwork *
GarnetNetworkParams::create()
{
    return new GarnetNetwork(this);
}

uint32_t
GarnetNetwork::functionalWrite(Packet *pkt)
{
    uint32_t num_functional_writes = 0;

    for (unsigned int i = 0; i < m_routers.size(); i++) {
        num_functional_writes += m_routers[i]->functionalWrite(pkt);
    }

    for (unsigned int i = 0; i < m_nis.size(); ++i) {
        num_functional_writes += m_nis[i]->functionalWrite(pkt);
    }

    for (unsigned int i = 0; i < m_networklinks.size(); ++i) {
        num_functional_writes += m_networklinks[i]->functionalWrite(pkt);
    }

    return num_functional_writes;
}
