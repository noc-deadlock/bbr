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


#include "mem/ruby/network/garnet2.0/Router.hh"

#include "base/stl_helpers.hh"
#include "debug/RubyNetwork.hh"
#include "mem/ruby/network/garnet2.0/CreditLink.hh"
#include "mem/ruby/network/garnet2.0/CrossbarSwitch.hh"
#include "mem/ruby/network/garnet2.0/GarnetNetwork.hh"
#include "mem/ruby/network/garnet2.0/InputUnit.hh"
#include "mem/ruby/network/garnet2.0/NetworkLink.hh"
#include "mem/ruby/network/garnet2.0/OutputUnit.hh"
#include "mem/ruby/network/garnet2.0/RoutingUnit.hh"
#include "mem/ruby/network/garnet2.0/SwitchAllocator.hh"

using namespace std;
using m5::stl_helpers::deletePointers;

Router::Router(const Params *p)
    : BasicRouter(p), Consumer(this)
{
    m_latency = p->latency;
    m_virtual_networks = p->virt_nets;
    m_vc_per_vnet = p->vcs_per_vnet;
    m_num_vcs = m_virtual_networks * m_vc_per_vnet;

    // initializing swizzleSwap parameters
    inport_occupancy = 0;
    is_critical = false;
    critical_inport.id = -1;
    critical_inport.dirn = "Unknown";
    critical_inport.send_credit = false;

    m_routing_unit = new RoutingUnit(this);
    m_sw_alloc = new SwitchAllocator(this);
    m_switch = new CrossbarSwitch(this);

    m_input_unit.clear();
    m_output_unit.clear();
}

Router::~Router()
{
    deletePointers(m_input_unit);
    deletePointers(m_output_unit);
    delete m_routing_unit;
    delete m_sw_alloc;
    delete m_switch;
}

void
Router::init()
{
    BasicRouter::init();

    // initialize your critical inport here
    // and then put an assert in the wakeup
    // that its never disabled and never points
    // to "Local_"
    if(get_net_ptr()->getPolicy() == MINIMAL_) {
        for(int inport=0; inport < m_input_unit.size(); inport++) {
            if(m_input_unit[inport]->get_direction() != "Local") {
                critical_inport.id = inport;
                critical_inport.dirn = m_input_unit[inport]->get_direction();
                assert(m_input_unit[inport]->vc_isEmpty(0) == true);
                break;
            }
        }
        // initialize outVcState of upstream router as well...
        Router* router_ = get_net_ptr()->\
                get_RouterInDirn(getInportDirection(critical_inport.id), m_id);
        // get outputUnit direction in router_
        PortDirection upstream_outputUnit_dirn =
                input_output_dirn_map(getInportDirection(critical_inport.id));
        // get id for that direction in router_
        int upstream_outputUnit_id =
                router_->m_routing_unit->m_outports_dirn2idx[upstream_outputUnit_dirn];

        router_->get_outputUnit_ref()[upstream_outputUnit_id]->set_vc_critical(0, true);
    }
    m_sw_alloc->init();
    m_switch->init();
}

void
Router::critical_swap(int critical_inport_id, int inport_id)
{
    assert(m_input_unit[inport_id]->vc_isEmpty(0) == false);
    // critical_inport_id must be empty
    // inport_id must NOT be empty... doSwap
    // doSwap.. just set the inport-vc active and idle accordingly..
    flit *t_flit;
    t_flit =  m_input_unit[inport_id]->getTopFlit(0);
    // insert the flit..
    m_input_unit[critical_inport_id]->insertFlit(0,t_flit);

    // update credit for both upstream routers...
    // increment credit for upstream router for inport_id's outVC
    Router* router1 = get_net_ptr()->\
                get_RouterInDirn(getInportDirection(inport_id), m_id);
    // get outputUnit direction in router1
    PortDirection upstream1_outputUnit_dirn =
                input_output_dirn_map(getInportDirection(inport_id));
    // get id for that direction in router1
    int upstream1_outputUnit = router1->m_routing_unit->\
                        m_outports_dirn2idx[upstream1_outputUnit_dirn];
    router1->get_outputUnit_ref()[upstream1_outputUnit]->\
                                        increment_credit(0);
    m_input_unit[inport_id]->set_vc_idle(0, curCycle()); // set this vc idle
    // update the outVcState for this router as well.. make it IDLE_
    router1->get_outputUnit_ref()[upstream1_outputUnit]->\
                           set_vc_state(IDLE_, 0, curCycle());
    // mark the critical outVc in upstream tourter as well...
    router1->get_outputUnit_ref()[upstream1_outputUnit]->\
                            set_vc_critical(0, true);
    /*-------------------------------------------------------------------------*/
    // decrement credit for upstream router for critical_inport_id's outVC
    Router* router2 = get_net_ptr()->\
            get_RouterInDirn(getInportDirection(critical_inport_id), m_id);
    // get outputUnit direction in router2
    PortDirection upstream2_outputUnit_dirn =
            input_output_dirn_map(getInportDirection(critical_inport_id));
    // get id for that direction in router2
    int upstream2_outputUnit = router2->m_routing_unit->\
                            m_outports_dirn2idx[upstream2_outputUnit_dirn];
    router2->get_outputUnit_ref()[upstream2_outputUnit]->\
                                        decrement_credit(0);
    //set vc state to be active
    m_input_unit[critical_inport_id]->set_vc_active(0, curCycle()); // set this vc active
    router2->get_outputUnit_ref()[upstream2_outputUnit]->\
                                set_vc_state(ACTIVE_, 0, curCycle());
    // unmark the outVc accordingly
    router2->get_outputUnit_ref()[upstream2_outputUnit]->\
                                set_vc_critical(0, false);

    // update the critical inport structure here...
    critical_inport.id = inport_id;
    critical_inport.dirn = getInportDirection(inport_id);

    return;
}

void
Router::bubble_deflect()
{
    // take the flit and swap...
    for(int inp_ = 0; inp_ < m_input_unit.size(); ++inp_) {
        if(getInportDirection(inp_) == "Local")
            continue;
        if(inp_ == critical_inport.id)
            continue;

        if(m_input_unit[inp_]->vc_isEmpty(0) == false) {
            // get the upstream router and do the swap
            // given upstream router's flit is not going
            // to local port...
            // 1. Try mutual routing..
            // 2. Try mis routing
            // 3. don't misroute "Local" outport flit
            #if (MY_PRINT)
            cout << "inp_: " << inp_ << " getInportDirection(inp_): " <<
                    getInportDirection(inp_) << endl;
            #endif
            Router* upstream_ = get_net_ptr()->\
                        get_RouterInDirn(getInportDirection(inp_), m_id);
            PortDirection towards_me_ = input_output_dirn_map(getInportDirection(inp_));
            std::vector<InputUnit *> upstream_inpUnit = upstream_->get_inputUnit_ref();
            // 1. mutual-routing loop
            int upstrm_inp_ = -1;
            for(upstrm_inp_ = 2;
                upstrm_inp_ < upstream_inpUnit.size();
                ++upstrm_inp_) {
                // check if anyflit has outport 'towads_me_' break and choose that flit
                if(upstream_inpUnit[upstrm_inp_]->vc_isEmpty(0) == false) {
                    if(upstream_inpUnit[upstrm_inp_]->\
                        peekTopFlit(0)->get_outport_dir() == towards_me_)
                        break;
                }
            }
            if(upstrm_inp_ < upstream_inpUnit.size()) {
                flit *t_flit1;
                flit *t_flit2;

                t_flit1 = m_input_unit[inp_]->getTopFlit(0);
                t_flit2 = upstream_inpUnit[upstrm_inp_]->getTopFlit(0);
                // Route computer for these flit respectively

                int outport2 = route_compute(t_flit2->get_route(),
                                            inp_,
                                            m_routing_unit->m_inports_idx2dirn[inp_]);
                t_flit2->set_outport(outport2);
                t_flit2->set_outport_dir(m_routing_unit->m_outports_idx2dirn[outport2]);
                // Swap
                m_input_unit[inp_]->insertFlit(0, t_flit2);

                int outport1 = upstream_->route_compute(t_flit1->get_route(),
                                            upstrm_inp_,
                                            upstream_->m_routing_unit->\
                                            m_inports_idx2dirn[upstrm_inp_]);
                t_flit1->set_outport(outport1);
                t_flit1->set_outport_dir(upstream_->m_routing_unit->\
                                            m_inports_idx2dirn[upstrm_inp_]);
                upstream_inpUnit[upstrm_inp_]->insertFlit(0, t_flit1);
                // routing for these flits..

                #if (MY_PRINT)
                cout << "Deflection successful via mutual routing..." << endl;
                #endif
                // update the stats:
                get_net_ptr()->num_routed_bubbleSwaps++;
                get_net_ptr()->num_bubbleSwaps++;
                // return
                return;
            }
            else {
                // Loop over all the inport of upstream router whichever doesn't have outportLocal
                // swap
                upstrm_inp_ = -1; // reset
                for(upstrm_inp_ = 2;
                    upstrm_inp_ < upstream_inpUnit.size();
                    ++upstrm_inp_) {
                    // check if anyflit has outport 'towads_me_' break and choose that flit
                    if(upstream_inpUnit[upstrm_inp_]->vc_isEmpty(0) == false) {
                        if(upstream_inpUnit[upstrm_inp_]->peekTopFlit(0)->get_outport_dir() != "Local")
                            break;
                    }
                }
                if(upstrm_inp_ < upstream_inpUnit.size()) {
                    flit *t_flit1;
                    flit *t_flit2;

                    t_flit1 = m_input_unit[inp_]->getTopFlit(0);
                    t_flit2 = upstream_inpUnit[upstrm_inp_]->getTopFlit(0);

                    // Swap
                    int outport2 = route_compute(t_flit2->get_route(),
                                                inp_,
                                                m_routing_unit->m_inports_idx2dirn[inp_]);
                    t_flit2->set_outport(outport2);
                    t_flit2->set_outport_dir(m_routing_unit->m_outports_idx2dirn[outport2]);
                    // Swap
                    m_input_unit[inp_]->insertFlit(0, t_flit2);

                    int outport1 = upstream_->route_compute(t_flit1->get_route(),
                                                upstrm_inp_,
                                                upstream_->m_routing_unit->\
                                                m_inports_idx2dirn[upstrm_inp_]);
                    t_flit1->set_outport(outport1);
                    t_flit1->set_outport_dir(upstream_->m_routing_unit->\
                                                m_inports_idx2dirn[upstrm_inp_]);
                    upstream_inpUnit[upstrm_inp_]->insertFlit(0, t_flit1);

                    #if (MY_PRINT)
                        cout << "Deflection successful via without routing..." << endl;
                    #endif
                    // Stats
                    get_net_ptr()->num_bubbleSwaps++;
                    // return
                    return;
                }
                else {

                    #if (MY_PRINT)
                        cout << "Deflection not successful..." << endl;
                    #endif
                    return;
                }
            }
        }
    }
    return;
}

// this api will loop through all the input port of the router (point by my_id);
// except critical and local inport
bool
Router::chk_critical_deflect(int my_id)
{
    std::vector<int> doDeflect;
    // 2 Local_ ports and 1 critical port
    // doDeflect.resize(m_input_unit.size() - 3);
    Router* router; // this is the router...
    for(int inport = 0; inport < m_input_unit.size(); ++inport) {
        // 2 Local_ direction
        if(getInportDirection(inport) == "Local")
            continue;
        // 1 critical port
        if(inport == critical_inport.id)
            continue;

        // populate doDeflect vector
        router = get_net_ptr()->get_RouterInDirn(getInportDirection(inport), my_id);
        // calculate occupancy of this router...
        std::vector<InputUnit *> input_unit_ = router->get_inputUnit_ref();
        int router_occupancy = 0;
        for(int input_port = 0; input_port < input_unit_.size(); ++input_port) {
            // calculate the occupancy of this router
            if(input_unit_[input_port]->vc_isEmpty(0) == false) {
                router_occupancy++;
            }
        }
        // after this loop if router_occupancy == N-2 then add the bit in 'doDeflect'
        // vector.
        if(router_occupancy == (input_unit_.size() - 2)) {
        #if (MY_PRINT)
            cout << "increasing the space of deflection vector"\
                " for doing bubble deflection" << endl;
        #endif
            doDeflect.push_back(1);
        }
    }
    // if the size of 'doDeflect' vector is N-3 then perform critical-bubble deflection.
    if(doDeflect.size() >= (m_input_unit.size() - 3)) {
        #if (MY_PRINT)
            cout << "<<<<<<<< Initiate the critical bubble deflect sequence >>>>>>>>" << endl;
        #endif
        return true;
    } else {
        #if (MY_PRINT)
            cout << "doDeflect.size(): " << doDeflect.size() << " and (m_input_unit.size() - 3) "\
                << (m_input_unit.size() - 3) << endl;
            cout << "therefore can't initiate the bubble deflection sequence "<< endl;
        #endif
        return false;
    }

}

void
Router::wakeup()
{
    DPRINTF(RubyNetwork, "Router %d woke up\n", m_id);

    #if (MY_PRINT)
    cout << "-------------------------" << endl;
    cout << "Router-" << m_id << " woke up at cycle: " << curCycle() << endl;
    cout << "-------------------------" << endl;
    #endif

    if(get_net_ptr()->isEnableSwizzleSwap() == true  &&
        get_net_ptr()->getPolicy() == MINIMAL_) {
        assert(m_input_unit[critical_inport.id]->vc_isEmpty(0) == true);
        assert(m_input_unit[critical_inport.id]->get_direction() != "Local");
        assert(m_input_unit[critical_inport.id]->get_direction() ==
            critical_inport.dirn);
        Router* router_ = get_net_ptr()->
            get_RouterInDirn(getInportDirection(critical_inport.id), m_id);
        PortDirection upstream_outputUnit_dirn =
            input_output_dirn_map(getInportDirection(critical_inport.id));
        int upstream_outputUnit_id =
            router_->m_routing_unit->\
                    m_outports_dirn2idx[upstream_outputUnit_dirn];
        assert(router_->get_outputUnit_ref()[upstream_outputUnit_id]->\
                                        is_vc_critical(0) == true);
        // assert that there's only one critical outVc among connecting routers...
        int critical_vc_cnt = 0;
        for(int inp_=0; inp_< m_input_unit.size(); ++inp_) {

            if(getInportDirection(inp_) == "Local")
                continue;

            Router* routr_ = get_net_ptr()->
                get_RouterInDirn(getInportDirection(inp_), m_id);
            PortDirection upstream_outputUnit_dirn =
                input_output_dirn_map(getInportDirection(inp_));
            int upstream_outputUnit_id =
                routr_->m_routing_unit->m_outports_dirn2idx[upstream_outputUnit_dirn];
            if(routr_->get_outputUnit_ref()[upstream_outputUnit_id]->is_vc_critical(0)
                == true) {
                critical_vc_cnt++;
                #if(MY_PRINT)
                cout << "critical_vc_cnt: " << critical_vc_cnt << " from router-id: "\
                    <<routr_->get_id()<< endl;
                #endif
            }
        }
        #if (MY_PRINT)
        cout << "critical_vc_cnt: " << critical_vc_cnt << endl;
        #endif
        assert(critical_vc_cnt == 1);

        // out_vc credit count corresponding to critical vc should always
        // be 1.
        Router* rout_ = get_net_ptr()->\
                    get_RouterInDirn(getInportDirection(critical_inport.id), m_id);

        PortDirection upstream_outputUnit_dir =
                input_output_dirn_map(getInportDirection(critical_inport.id));
        // get id for that direction in router_
        int upstrm_outputUnit_id =
                rout_->m_routing_unit->m_outports_dirn2idx[upstream_outputUnit_dir];
        assert(rout_->get_outputUnit_ref()\
            [upstrm_outputUnit_id]->get_credit_count(0) == 1);

    }

    int router_occupancy = 0;
    // check for incoming flits
    for (int inport = 0; inport < m_input_unit.size(); inport++) {
        m_input_unit[inport]->wakeup();
        if (m_input_unit[inport]->vc_isEmpty(0) == false)
            router_occupancy++;
    }

    // Now all packets in the input port has been put from the links...
    // do the swizzleSwap here with differnt options..
    // next option is to always keep doing swap instead of doing at TDM_
    // if((curCycle()%(get_net_ptr()->getNumRouters())) == m_id) {
    // We will swap everytime the router wakesup()
    if(get_net_ptr()->isEnableSwizzleSwap()) {
        // option-1: Minimal
        if(get_net_ptr()->getPolicy() == MINIMAL_) {
            int success = -1;

            // just incremennt the global counter whenever
            // 'swapInport'returns either 1 or 2
            // keep a counter for incrementing indivdually as well
            if(this->is_myTurn() == true) {
                #if (MY_PRINT)
                cout << "Doing swizzle at cycle: " << curCycle() << endl;
                #endif
                success = swapInport();
            }
            if(success == 1) {
                get_net_ptr()->num_bubbleSwizzles++;

                #if (MY_PRINT)
                cout << "Swizzle completed with empty input-port..." << endl;
                #endif
            }
            else if (success == 2) {
                get_net_ptr()->num_bubbleSwizzles++;
                #if (MY_PRINT)
                cout << "Swizzle completed with flit with differnt outport"<< endl;
                #endif
            }
            else if (success == 0) {
                #if (MY_PRINT)
                cout << "Swizzle couldn't be completed..." << endl;
                #endif
            }
            else {
                #if (MY_PRINT)
                cout << "not cycle-turn for doing swap..." << endl;
                #endif
            }
            #if (MY_PRINT)
            cout << "router_occupancy: "<< router_occupancy << " inputUnit.size(): "\
                << m_input_unit.size() << " (m_input_unit.size()-2): " << (m_input_unit.size()-2)\
                << endl;
            #endif
            if(router_occupancy == (m_input_unit.size()-2)) {
                // check the occupancy at the router pointed by each outport
                // of the flit present in this router..
                bool doCriticalDeflect = false;
                doCriticalDeflect = chk_critical_deflect(m_id);
                if(doCriticalDeflect == true) {
                    #if (MY_PRINT)
                    cout << "do bubble deflection " << endl;
                    #endif
                    bubble_deflect();
                } else {
                    #if (MY_PRINT)
                    cout << "don't do bubble deflection " << endl;
                    #endif
                }

            }
        } // option-2: Non-Minimal
        else if(get_net_ptr()->getPolicy() == NON_MINIMAL_) {
            // Deflection...
            fatal("Deflection Not working... \n");
            // re-compute the route for all the flits again in deflection.
            // whichever is non-empty
            for (int inport = 0; inport < m_input_unit.size(); inport++) {
                if (m_input_unit[inport]->vc_isEmpty(0) == false &&
                    m_input_unit[inport]->get_direction() != "Local") {
                    // getTopFlit.. recomute route and insertit back again...
                    flit* t_flit = m_input_unit[inport]->getTopFlit(0);
                    int outport;
//                    int toss = random() % 10;
//                    if( toss == 1) {
//                        // outport can't be 0
//                        outport = (random() % (m_output_unit.size() - 2)) + 2;
//                    } else {
                    cout << "Re-computing outport for Router: " << m_id << endl;
                    outport = route_compute(t_flit->get_route(),
                                    inport, getInportDirection(inport));
//                    }
                    // set the outport in the flit as well as the direction of
                    // hte outport in the flit
                    t_flit->set_outport(outport);
                    t_flit->set_outport_dir(getOutportDirection(outport));
                    m_input_unit[inport]->insertFlit(0, t_flit);
                }
            }

//            fatal("Not implemented \n"); // implement deflection here
        }
    }
    // }
    // check for incoming credits
    // Note: the credit update is happening before SA
    // buffer turnaround time =
    //     credit traversal (1-cycle) + SA (1-cycle) + Link Traversal (1-cycle)
    // if we want the credit update to take place after SA, this loop should
    // be moved after the SA request
    for (int outport = 0; outport < m_output_unit.size(); outport++) {
        m_output_unit[outport]->wakeup();
    }

    // Switch Allocation
    m_sw_alloc->wakeup();

    // Switch Traversal
    m_switch->wakeup();

    // calculate inport occupancy of the router...
    // reset:
    inport_occupancy = 0;
    for (int inport = 0; inport < m_input_unit.size(); inport++) {
        if (m_input_unit[inport]->vc_isEmpty(0) == false)
            inport_occupancy++;
    }

}


// Rules for swap:
// 0. choose the inport randomly to swap from..
// 1. if there's an empty inport present then first swap from there..
//      then take care of credit signalling in the outVC state of
//      respective routers
// 2. else swap with the filled ones and then you don't need to take
//     care of the credit signalling...

// Note: implement peekTopFlit on linkClass as well.. if there's already
// packet sitting on the link then don't decrement credits for the bubble...
int
Router::swapInport() {

    // If my router is empty then don't do swaps..
    int inport_;
    for( inport_ = 0; inport_ < m_input_unit.size(); inport_++) {
        if(m_input_unit[inport_]->vc_isEmpty(0) == false)
            break;
    }
    if(inport_ == m_input_unit.size())
        return 0; // don't swap for an empty router..

    // two inport-id from where we need to swap
    int inport_full = -1;
    int inport_empty = -1;

    // you already have a critical inport.. just need to find another inport
    // to swap this critical inport..
    // 1. It should not be Local_
    // 2. If it's empty then make sure there is nothing on the link
    // 3. or it could be non-empty (in which case use critical_swap()-api)
    int itr_cnt = 0;
    while(1) {
        itr_cnt++;
        if(itr_cnt >= 50)
            return 0; // unsuccessful
        // choose a random inport
        int inport_ = random() % (m_input_unit.size());
        // do not swap if its local_
        if(getInportDirection(inport_) == "Local")
            continue;

        // either empty -- break;
        if((m_input_unit[inport_]->vc_isEmpty(0) == true) &&
            (getInportDirection(inport_) != "Local") &&
            (inport_ != critical_inport.id) ) {

            Router* router2 = get_net_ptr()->\
                get_RouterInDirn(getInportDirection(inport_), m_id);
            PortDirection upstream2_outputUnit_dirn = input_output_dirn_map(getInportDirection(inport_));
            int upstream2_outputUnit = router2->m_routing_unit->m_outports_dirn2idx[upstream2_outputUnit_dirn];
            if (router2->get_outputUnit_ref()[upstream2_outputUnit]->is_vc_idle(0, curCycle()) == false)
                continue; // not possible so break... instread of continue

            inport_empty = inport_;
            break;
        }

        // Or filled -- break;
        if((m_input_unit[inport_]->vc_isEmpty(0) == false) &&
            (getInportDirection(inport_) != "Local") &&
            (inport_ != critical_inport.id)) {
            inport_full = inport_;
            break;
        }
    }

    assert(((inport_empty != -1) && (inport_full == -1)) ||
            ((inport_empty == -1) && (inport_full != -1)));

    // doShuffle...
    if(inport_empty == -1) {
        //swap between 'input_full' and 'critical id'
        critical_swap(critical_inport.id, inport_full);
        return 2;
    }
    else if(inport_full == -1) {

        Router* router1 = get_net_ptr()->\
            get_RouterInDirn(getInportDirection(inport_empty), m_id);
        // get outputUnit direction in router1
        PortDirection upstream1_outputUnit_dirn =
                input_output_dirn_map(getInportDirection(inport_empty));
        // get id for that direction in router1
        int upstream1_outputUnit =
                router1->m_routing_unit->\
                m_outports_dirn2idx[upstream1_outputUnit_dirn];
        // mark the critical outVc in upstream tourter as well...
        router1->get_outputUnit_ref()[upstream1_outputUnit]->\
                                        set_vc_critical(0, true);
        /*-------------------------------------------------------------------------*/
        // decrement credit for upstream router for critical_inport_id's outVC
        Router* router2 = get_net_ptr()->\
                get_RouterInDirn(getInportDirection(critical_inport.id), m_id);
        // get outputUnit direction in router2
        PortDirection upstream2_outputUnit_dirn =
                input_output_dirn_map(getInportDirection(critical_inport.id));
        // get id for that direction in router2
        int upstream2_outputUnit =
                router2->m_routing_unit->\
                m_outports_dirn2idx[upstream2_outputUnit_dirn];
        // unmark the outVc accordingly
        router2->get_outputUnit_ref()[upstream2_outputUnit]->\
                                        set_vc_critical(0, false);

        // update the critical inport structure here...
        critical_inport.id = inport_empty;
        critical_inport.dirn = getInportDirection(inport_empty);
        return 1;
    }

    return 0;
}


int
Router::get_numFreeVC(PortDirection dirn_) {
    // Caution: This 'dirn_' is the direction of inport
    // of downstream router...
    assert(dirn_ != "Local");
    int inport_id = m_routing_unit->m_inports_dirn2idx[dirn_];
    return (m_input_unit[inport_id]->get_numFreeVC(dirn_));
}


void
Router::addInPort(PortDirection inport_dirn,
                  NetworkLink *in_link, CreditLink *credit_link)
{
    int port_num = m_input_unit.size();
    InputUnit *input_unit = new InputUnit(port_num, inport_dirn, this);

    input_unit->set_in_link(in_link);
    input_unit->set_credit_link(credit_link);
    in_link->setLinkConsumer(this);
    credit_link->setSourceQueue(input_unit->getCreditQueue());

    m_input_unit.push_back(input_unit);

    m_routing_unit->addInDirection(inport_dirn, port_num);
}

void
Router::addOutPort(PortDirection outport_dirn,
                   NetworkLink *out_link,
                   const NetDest& routing_table_entry, int link_weight,
                   CreditLink *credit_link)
{
    int port_num = m_output_unit.size();
    OutputUnit *output_unit = new OutputUnit(port_num, outport_dirn, this);

    output_unit->set_out_link(out_link);
    output_unit->set_credit_link(credit_link);
    credit_link->setLinkConsumer(this);
    out_link->setSourceQueue(output_unit->getOutQueue());

    m_output_unit.push_back(output_unit);

    m_routing_unit->addRoute(routing_table_entry);
    m_routing_unit->addWeight(link_weight);
    m_routing_unit->addOutDirection(outport_dirn, port_num);
}

PortDirection
Router::getOutportDirection(int outport)
{
    return m_output_unit[outport]->get_direction();
}

PortDirection
Router::getInportDirection(int inport)
{
    return m_input_unit[inport]->get_direction();
}

int
Router::route_compute(RouteInfo route, int inport, PortDirection inport_dirn)
{
    return m_routing_unit->outportCompute(route, inport, inport_dirn);
}

void
Router::grant_switch(int inport, flit *t_flit)
{
    m_switch->update_sw_winner(inport, t_flit);
}

void
Router::schedule_wakeup(Cycles time)
{
    // wake up after time cycles
    scheduleEvent(time);
}

std::string
Router::getPortDirectionName(PortDirection direction)
{
    // PortDirection is actually a string
    // If not, then this function should add a switch
    // statement to convert direction to a string
    // that can be printed out
    return direction;
}

void
Router::regStats()
{
    BasicRouter::regStats();

    m_buffer_reads
        .name(name() + ".buffer_reads")
        .flags(Stats::nozero)
    ;

    m_buffer_writes
        .name(name() + ".buffer_writes")
        .flags(Stats::nozero)
    ;

    m_crossbar_activity
        .name(name() + ".crossbar_activity")
        .flags(Stats::nozero)
    ;

    m_sw_input_arbiter_activity
        .name(name() + ".sw_input_arbiter_activity")
        .flags(Stats::nozero)
    ;

    m_sw_output_arbiter_activity
        .name(name() + ".sw_output_arbiter_activity")
        .flags(Stats::nozero)
    ;
}

void
Router::collateStats()
{
    for (int j = 0; j < m_virtual_networks; j++) {
        for (int i = 0; i < m_input_unit.size(); i++) {
            m_buffer_reads += m_input_unit[i]->get_buf_read_activity(j);
            m_buffer_writes += m_input_unit[i]->get_buf_write_activity(j);
        }
    }

    m_sw_input_arbiter_activity = m_sw_alloc->get_input_arbiter_activity();
    m_sw_output_arbiter_activity = m_sw_alloc->get_output_arbiter_activity();
    m_crossbar_activity = m_switch->get_crossbar_activity();
}

void
Router::resetStats()
{
    for (int j = 0; j < m_virtual_networks; j++) {
        for (int i = 0; i < m_input_unit.size(); i++) {
            m_input_unit[i]->resetStats();
        }
    }

    m_switch->resetStats();
    m_sw_alloc->resetStats();
}

void
Router::printFaultVector(ostream& out)
{
    int temperature_celcius = BASELINE_TEMPERATURE_CELCIUS;
    int num_fault_types = m_network_ptr->fault_model->number_of_fault_types;
    float fault_vector[num_fault_types];
    get_fault_vector(temperature_celcius, fault_vector);
    out << "Router-" << m_id << " fault vector: " << endl;
    for (int fault_type_index = 0; fault_type_index < num_fault_types;
         fault_type_index++) {
        out << " - probability of (";
        out <<
        m_network_ptr->fault_model->fault_type_to_string(fault_type_index);
        out << ") = ";
        out << fault_vector[fault_type_index] << endl;
    }
}

void
Router::printAggregateFaultProbability(std::ostream& out)
{
    int temperature_celcius = BASELINE_TEMPERATURE_CELCIUS;
    float aggregate_fault_prob;
    get_aggregate_fault_probability(temperature_celcius,
                                    &aggregate_fault_prob);
    out << "Router-" << m_id << " fault probability: ";
    out << aggregate_fault_prob << endl;
}

uint32_t
Router::functionalWrite(Packet *pkt)
{
    uint32_t num_functional_writes = 0;
    num_functional_writes += m_switch->functionalWrite(pkt);

    for (uint32_t i = 0; i < m_input_unit.size(); i++) {
        num_functional_writes += m_input_unit[i]->functionalWrite(pkt);
    }

    for (uint32_t i = 0; i < m_output_unit.size(); i++) {
        num_functional_writes += m_output_unit[i]->functionalWrite(pkt);
    }

    return num_functional_writes;
}

Router *
GarnetRouterParams::create()
{
    return new Router(this);
}
