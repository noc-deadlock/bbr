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

    m_sw_alloc->init();
    m_switch->init();
}

void
Router::wakeup()
{
    DPRINTF(RubyNetwork, "Router %d woke up\n", m_id);

    // 0. even before all this.. every router wakes up and check if there are
    // more than 1 critical router surrounding it.. if yes.. then it switches
    // all of them off but 1.
    if(this->is_critical == false) {
        vector<Router*> critical_routers;
//        critical_routers.resize(4);
        for(int inport = 0; inport < m_input_unit.size(); inport++) {
            PortDirection dirn = m_routing_unit->m_inports_idx2dirn[inport];
            if((dirn == "North") || (dirn == "East") ||
                (dirn == "West") || (dirn == "South")) {
               Router* router =  get_net_ptr()->get_downstreamRouter(dirn, m_id);
                if(router->is_critical == true)
                    critical_routers.push_back(router);
            }
        }
        // this is the place where you.. switch off all but one
        if(critical_routers.size() > 1) {
            for(int idx = 1; idx < critical_routers.size(); ++idx) {
                stablizeCriticalRouter(critical_routers[idx]);
            }
        }
    }
    // check if it is critical
    if (this->is_critical == true) {
        // 1. assert that at least one of the (N_;W_;E_;S_) outport is empty
        int free_inport = 0;
        for(int inport = 0; inport < m_input_unit.size(); inport++) {
            PortDirection dirn = m_routing_unit->m_inports_idx2dirn[inport];
            if((dirn == "North") || (dirn == "East") ||
                (dirn == "West") || (dirn == "South"))
                if(m_input_unit[inport]->vc_isEmpty(0) == true)
                    free_inport++;
        }
        assert(free_inport > 0);

        // 2. assert that there won't be any adjoining critical router...
        // note: if there's inport, then there's a router in that direction.
        for(int inport = 0; inport < m_input_unit.size(); inport++) {
            PortDirection dirn = m_routing_unit->m_inports_idx2dirn[inport];
            if((dirn == "North") || (dirn == "East") ||
                (dirn == "West") || (dirn == "South")) {
               Router* router =  get_net_ptr()->get_downstreamRouter(dirn, m_id);
               assert(router->is_critical == false);
            }
        }

        // 3. more sanity checks:
        assert(critical_inport.id != -1);
        assert(critical_inport.dirn != "Unknown");
    }

    int router_occupancy = 0; // this counts the number of inports currently
                            // occupied in router..
    // Router looses the 'criticality'-state here... essentially
    // it checks if there's any router in my neighbor  which is cretical
    // then if it is also critical.. then it becomes non-cretical.

    // check for incoming flits
    for (int inport = 0; inport < m_input_unit.size(); inport++) {
        // 4. If critical; never consume flit from the link..
        if((is_critical == true) && (inport == critical_inport.id)) {
            // 5. check if flit is present on the link.. if yes then
            // no credit signalling is required...
            // set the structure accordingly.
            if (m_input_unit[inport]->m_in_link->isReady(curCycle()))
                critical_inport.send_credit = false; // no need to send credit
            else
                critical_inport.send_credit = true; // decrement credit to upstream router
                                                   // with probably setting 'isFree' signal false.
            // continue...
            continue;

        }
        m_input_unit[inport]->wakeup();
        if (m_input_unit[inport]->vc_isEmpty(0) == false)
            router_occupancy++;
        if(router_occupancy == (m_input_unit.size() - 1)) {
          // do the analysis here..
          // check if this router is critical router..
          // if there's already a critical router present then
          // make this router non-critical. (also change critical_inport = -1)
          // if yes, then
          // skip the iteration

          // else
          // check if any adjacent router is critical router? if yes..
          // go for the next iteration..

          // else make this router as
          // critical router.. the necessary condition condition for
          // a router to be critical router is that it should have
          // necessarily have one free port..
        }
    }

    // Now all packets in the input port has been put from the links...
    // do the swizzleSwap here with differnt options..
    // next option is to always keep doing swap instead of doing at TDM_
    // if((curCycle()%(get_net_ptr()->getNumRouters())) == m_id) {
    // We will swap everytime the router wakesup()
    if(get_net_ptr()->isEnableSwizzleSwap()) {
        // option-1: Minimal
        if(get_net_ptr()->getPolicy() == MINIMAL_) {
            int success = swapInport();

            if(success)
                cout << "Swap successfully completed..." << endl;
            else
                cout << "Swap couldn't be completed..." << endl;

        } // option-2: Non-Minimal
        else if(get_net_ptr()->getPolicy() == NON_MINIMAL_) {
            fatal("Not implemented \n");
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
    // swap inport of these routers...
//    for(int inport = 0; inport < m_input_unit.size(); inport++) {
        // see if you can do swap here..
//        m_input_unit[inport]

//    }

    return 0;
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
