/////////////////////////////////////////////////////////////////////////////
//
// Copyright (c) 2019, The Regents of the University of California
// All rights reserved.
//
// BSD 3-Clause License
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice, this
//   list of conditions and the following disclaimer.
//
// * Redistributions in binary form must reproduce the above copyright notice,
//   this list of conditions and the following disclaimer in the documentation
//   and/or other materials provided with the distribution.
//
// * Neither the name of the copyright holder nor the names of its
//   contributors may be used to endorse or promote products derived from
//   this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
///////////////////////////////////////////////////////////////////////////////

#include "RepairHold.hh"
#include "rsz/Resizer.hh"
#include "RepairDesign.hh"

#include "utl/Logger.h"
#include "db_sta/dbNetwork.hh"

#include "sta/Units.hh"
#include "sta/PortDirection.hh"
#include "sta/Liberty.hh"
#include "sta/TimingArc.hh"
#include "sta/Graph.hh"
#include "sta/DcalcAnalysisPt.hh"
#include "sta/GraphDelayCalc.hh"
#include "sta/Parasitics.hh"
#include "sta/Sdc.hh"
#include "sta/InputDrive.hh"
#include "sta/Corner.hh"
#include "sta/PathVertex.hh"
#include "sta/PathRef.hh"
#include "sta/PathExpanded.hh"
#include "sta/Fuzzy.hh"
#include "sta/Search.hh"

namespace rsz {

using std::abs;
using std::min;
using std::max;
using std::string;
using std::vector;
using std::map;
using std::pair;

using utl::RSZ;

using sta::Port;
using sta::VertexOutEdgeIterator;
using sta::Edge;
using sta::Clock;
using sta::PathExpanded;
using sta::INF;
using sta::fuzzyEqual;
using sta::fuzzyLess;
using sta::fuzzyLessEqual;
using sta::fuzzyGreater;
using sta::fuzzyGreaterEqual;
using sta::Unit;
using sta::Corners;
using sta::InputDrive;

RepairHold::RepairHold() :
  StaState(),
  logger_(nullptr),
  sta_(nullptr),
  db_network_(nullptr),
  resizer_(nullptr),
  resize_count_(0),
  inserted_buffer_count_(0),
  min_(MinMax::min()),
  max_(MinMax::max())
{
}

void
RepairHold::init(Resizer *resizer)
{
  resizer_ = resizer;
  logger_ = resizer->logger_;
  sta_ = resizer->sta_;
  db_network_ = resizer->db_network_;

  copyState(sta_);
}

void
RepairHold::repairHold(float slack_margin,
                       bool allow_setup_violations,
                       // Max buffer count as percent of design instance count.
                       float max_buffer_percent,
                       int max_passes)
{
  resizer_->init();
  sta_->checkSlewLimitPreamble();
  sta_->checkCapacitanceLimitPreamble();
  LibertyCell *buffer_cell = findHoldBuffer();

  sta_->findRequireds();
  VertexSet *ends = sta_->search()->endpoints();
  VertexSeq ends1;
  for (Vertex *end : *ends)
    ends1.push_back(end);
  sort(ends1, sta::VertexIdLess(graph_));

  int max_buffer_count = max_buffer_percent * network_->instanceCount();
  resizer_->incrementalParasiticsBegin();
  repairHold(ends1, buffer_cell, slack_margin,
             allow_setup_violations, max_buffer_count, max_passes);

  // Leave the parasitices up to date.
  resizer_->updateParasitics();
  resizer_->incrementalParasiticsEnd();
}

// For testing/debug.
void
RepairHold::repairHold(Pin *end_pin,
                       float slack_margin,
                       bool allow_setup_violations,
                       float max_buffer_percent,
                       int max_passes)
{
  resizer_->init();
  sta_->checkSlewLimitPreamble();
  sta_->checkCapacitanceLimitPreamble();
  LibertyCell *buffer_cell = findHoldBuffer();

  Vertex *end = graph_->pinLoadVertex(end_pin);
  VertexSeq ends;
  ends.push_back(end);

  sta_->findRequireds();
  int max_buffer_count = max_buffer_percent * network_->instanceCount();
  resizer_->incrementalParasiticsBegin();
  repairHold(ends, buffer_cell, slack_margin, allow_setup_violations,
             max_buffer_count, max_passes);
  // Leave the parasitices up to date.
  resizer_->updateParasitics();
  resizer_->incrementalParasiticsEnd();
}

// Find the buffer with the most delay in the fastest corner.
LibertyCell *
RepairHold::findHoldBuffer()
{
  LibertyCell *max_buffer = nullptr;
  float max_delay = 0.0;
  for (LibertyCell *buffer : resizer_->buffer_cells_) {
    float buffer_min_delay = bufferHoldDelay(buffer);
    if (max_buffer == nullptr
        || buffer_min_delay > max_delay) {
      max_buffer = buffer;
      max_delay = buffer_min_delay;
    }
  }
  return max_buffer;
}

float
RepairHold::bufferHoldDelay(LibertyCell *buffer)
{
  Delay delays[RiseFall::index_count];
  bufferHoldDelays(buffer, delays);
  return min(delays[RiseFall::riseIndex()],
             delays[RiseFall::fallIndex()]);
}

// Min self delay across corners; buffer -> buffer
void
RepairHold::bufferHoldDelays(LibertyCell *buffer,
                             // Return values.
                             Delay delays[RiseFall::index_count])
{
  LibertyPort *input, *output;
  buffer->bufferPorts(input, output);

  for (int rf_index : RiseFall::rangeIndex())
    delays[rf_index] = MinMax::min()->initValue();
  for (Corner *corner : *sta_->corners()) {
    LibertyPort *corner_port = input->cornerPort(corner->libertyIndex(max_));
    const DcalcAnalysisPt *dcalc_ap = corner->findDcalcAnalysisPt(max_);
    float load_cap = corner_port->capacitance();
    ArcDelay gate_delays[RiseFall::index_count];
    Slew slews[RiseFall::index_count];
    resizer_->gateDelays(output, load_cap, dcalc_ap, gate_delays, slews);
    for (int rf_index : RiseFall::rangeIndex())
      delays[rf_index] = min(delays[rf_index], gate_delays[rf_index]);
  }
}

void
RepairHold::repairHold(VertexSeq &ends,
                       LibertyCell *buffer_cell,
                       float slack_margin,
                       bool allow_setup_violations,
                       int max_buffer_count,
                       int max_passes)
{
  // Find endpoints with hold violations.
  VertexSeq hold_failures;
  Slack worst_slack;
  findHoldViolations(ends, slack_margin, worst_slack, hold_failures);
  if (!hold_failures.empty()) {
    logger_->info(RSZ, 46, "Found {} endpoints with hold violations.",
                  hold_failures.size());
    inserted_buffer_count_ = 0;
    bool progress = true;
    int pass = 1;
    while (worst_slack < 0.0
           && progress
           && !resizer_->overMaxArea()
           && inserted_buffer_count_ <= max_buffer_count
           && pass <= max_passes) {
      debugPrint(logger_, RSZ, "repair_hold", 1,
                 "pass {} worst slack {}",
                 pass,
                 delayAsString(worst_slack, sta_, 3));
      int hold_buffer_count_before = inserted_buffer_count_;
      repairHoldPass(hold_failures, buffer_cell, slack_margin,
                     allow_setup_violations, max_buffer_count);
      debugPrint(logger_, RSZ, "repair_hold", 1, "inserted {}",
                 inserted_buffer_count_ - hold_buffer_count_before);
      sta_->findRequireds();
      findHoldViolations(ends, slack_margin, worst_slack, hold_failures);
      pass++;
      progress = inserted_buffer_count_ > hold_buffer_count_before;
    }
    if (slack_margin == 0.0 && fuzzyLess(worst_slack, 0.0))
      logger_->warn(RSZ, 66, "Unable to repair all hold violations.");
    else if (fuzzyLess(worst_slack, slack_margin))
      logger_->warn(RSZ, 64, "Unable to repair all hold checks within margin.");

    if (inserted_buffer_count_ > 0) {
      logger_->info(RSZ, 32, "Inserted {} hold buffers.", inserted_buffer_count_);
      resizer_->level_drvr_vertices_valid_ = false;
    }
    if (inserted_buffer_count_ > max_buffer_count)
      logger_->error(RSZ, 60, "Max buffer count reached.");
    if (resizer_->overMaxArea())
      logger_->error(RSZ, 50, "Max utilization reached.");
  }
  else
    logger_->info(RSZ, 33, "No hold violations found.");
}

void
RepairHold::findHoldViolations(VertexSeq &ends,
                               float slack_margin,
                               // Return values.
                               Slack &worst_slack,
                               VertexSeq &hold_violations)
{
  worst_slack = INF;
  hold_violations.clear();
  debugPrint(logger_, RSZ, "repair_hold", 3, "Hold violations");
  for (Vertex *end : ends) {
    Slack slack = sta_->vertexSlack(end, min_) - slack_margin;
    if (!sta_->isClock(end->pin())
        && slack < 0.0) {
      debugPrint(logger_, RSZ, "repair_hold", 3, " {}",
                 end->name(sdc_network_));
      if (slack < worst_slack)
        worst_slack = slack;
      hold_violations.push_back(end);
    }
  }
}

void
RepairHold::repairHoldPass(VertexSeq &hold_failures,
                           LibertyCell *buffer_cell,
                           float slack_margin,
                           bool allow_setup_violations,
                           int max_buffer_count)
{
  resizer_->updateParasitics();
  sort(hold_failures, [=] (Vertex *end1,
                           Vertex *end2) {
    return sta_->vertexSlack(end1, min_) < sta_->vertexSlack(end2, min_);
  });
  for (Vertex *end_vertex : hold_failures) {
    resizer_->updateParasitics();
    repairEndHold(end_vertex, buffer_cell, slack_margin,
                  allow_setup_violations, max_buffer_count);
  }
}

void
RepairHold::repairEndHold(Vertex *end_vertex,
                          LibertyCell *buffer_cell,
                          float slack_margin,
                          bool allow_setup_violations,
                          int max_buffer_count)
{
  PathRef end_path = sta_->vertexWorstSlackPath(end_vertex, min_);
  if (!end_path.isNull()) {
    Slack end_hold_slack = end_path.slack(sta_);
    debugPrint(logger_, RSZ, "repair_hold", 3, "repair end {} hold_slack={}",
               end_vertex->name(network_),
               delayAsString(end_hold_slack, sta_));
    PathExpanded expanded(&end_path, sta_);
    sta::SearchPredNonLatch2 pred(sta_);
    int path_length = expanded.size();
    if (path_length > 1) {
      int min_index = MinMax::minIndex();
      int max_index = MinMax::maxIndex();
      for (int i = expanded.startIndex(); i < path_length; i++) {
        PathRef *path = expanded.path(i);
        Vertex *path_vertex = path->vertex(sta_);
        if (path_vertex->isDriver(network_)) {
          const RiseFall *path_rf = path->transition(sta_);
          PinSeq load_pins;
          float load_cap = 0.0;
          bool loads_have_out_port = false;
          VertexOutEdgeIterator edge_iter(path_vertex, graph_);
          while (edge_iter.hasNext()) {
            Edge *edge = edge_iter.next();
            Vertex *fanout = edge->to(graph_);
            if (pred.searchTo(fanout)
                && pred.searchThru(edge)) {
              Slack fanout_hold_slack = sta_->vertexSlack(fanout, min_)
                - slack_margin;
              if (fanout_hold_slack < 0.0) {
                Pin *load_pin = fanout->pin();
                load_pins.push_back(load_pin);
                if (network_->direction(load_pin)->isAnyOutput()
                    && network_->isTopLevelPort(load_pin))
                  loads_have_out_port = true;
                else {
                  LibertyPort *load_port = network_->libertyPort(load_pin);
                  if (load_port)
                    load_cap += load_port->capacitance();
                }
              }
            }
          }
          if (!load_pins.empty()) {
            Slack path_slacks[RiseFall::index_count][MinMax::index_count];
            sta_->vertexSlacks(path_vertex, path_slacks);
            Slack hold_slack  = path_slacks[path_rf->index()][min_index] - slack_margin;
            Slack setup_slack = path_slacks[path_rf->index()][max_index];;
            debugPrint(logger_, RSZ,
                       "repair_hold", 3, " {} hold_slack={} setup_slack={} fanouts={}",
                       path_vertex->name(network_),
                       delayAsString(hold_slack, sta_),
                       delayAsString(setup_slack, sta_),
                       load_pins.size());
            const DcalcAnalysisPt *dcalc_ap = sta_->cmdCorner()->findDcalcAnalysisPt(max_);
            Delay buffer_delay = resizer_->bufferDelay(buffer_cell, path_rf,
                                                       load_cap, dcalc_ap);
            if (setup_slack > -hold_slack
                // enough slack to insert a buffer
                && setup_slack > buffer_delay) {
              Vertex *path_load = expanded.path(i + 1)->vertex(sta_);
              Point path_load_loc = db_network_->location(path_load->pin());
              Point drvr_loc = db_network_->location(path_vertex->pin());
              Point buffer_loc((drvr_loc.x() + path_load_loc.x()) / 2,
                               (drvr_loc.y() + path_load_loc.y()) / 2);
              makeHoldDelay(path_vertex, load_pins, loads_have_out_port,
                            buffer_cell, buffer_loc);
            }
          }
        }
      }
    }
  }
}

void
RepairHold::makeHoldDelay(Vertex *drvr,
                          PinSeq &load_pins,
                          bool loads_have_out_port,
                          LibertyCell *buffer_cell,
                          Point loc)
{
  Pin *drvr_pin = drvr->pin();
  Instance *parent = db_network_->topInstance();
  Net *drvr_net = network_->isTopLevelPort(drvr_pin)
    ? db_network_->net(db_network_->term(drvr_pin))
    : db_network_->net(drvr_pin);
  Net *in_net, *out_net;
  if (loads_have_out_port) {
    // Verilog uses nets as ports, so the net connected to an output port has
    // to be preserved.
    // Move the driver pin over to gensym'd net.
    in_net = resizer_->makeUniqueNet();
    Port *drvr_port = network_->port(drvr_pin);
    Instance *drvr_inst = network_->instance(drvr_pin);
    sta_->disconnectPin(drvr_pin);
    sta_->connectPin(drvr_inst, drvr_port, in_net);
    out_net = drvr_net;
  }
  else {
    in_net = drvr_net;
    out_net = resizer_->makeUniqueNet();
  }

  resizer_->parasiticsInvalid(in_net);

  Net *buf_in_net = in_net;
  Instance *buffer = nullptr;
  LibertyPort *input, *output;
  buffer_cell->bufferPorts(input, output);
  // drvr_pin->drvr_net->hold_buffer->net2->load_pins
  string buffer_name = resizer_->makeUniqueInstName("hold");
  buffer = resizer_->makeInstance(buffer_cell, buffer_name.c_str(), parent);
  resizer_->journalMakeBuffer(buffer);
  inserted_buffer_count_++;
  resizer_->designAreaIncr(resizer_->area(db_network_->cell(buffer_cell)));

  sta_->connectPin(buffer, input, buf_in_net);
  sta_->connectPin(buffer, output, out_net);
  resizer_->setLocation(buffer, loc);
  resizer_->parasiticsInvalid(out_net);

  for (Pin *load_pin : load_pins) {
    Net *load_net = network_->isTopLevelPort(load_pin)
      ? network_->net(network_->term(load_pin))
      : network_->net(load_pin);
    if (load_net != out_net) {
      Instance *load = db_network_->instance(load_pin);
      Port *load_port = db_network_->port(load_pin);
      sta_->disconnectPin(load_pin);
      sta_->connectPin(load, load_port, out_net);
    }
  }

  Pin *buffer_out_pin = network_->findPin(buffer, output);
  resizer_->updateParasitics();
  Vertex *buffer_out_vertex = graph_->pinDrvrVertex(buffer_out_pin);
  sta_->findDelays(buffer_out_vertex);
  if (!checkMaxSlewCap(buffer_out_pin))
    resizer_->resizeToTargetSlew(buffer_out_pin, resize_count_);
}

bool
RepairHold::checkMaxSlewCap(const Pin *drvr_pin)
{
  float cap, limit, slack;
  const Corner *corner;
  const RiseFall *tr;
  sta_->checkCapacitance(drvr_pin, nullptr, max_,
                         corner, tr, cap, limit, slack);
  float slack_limit_ratio = slack / limit;
  if (slack_limit_ratio < hold_slack_limit_ratio_max_)
    return false;

  Slew slew;
  sta_->checkSlew(drvr_pin, nullptr, max_, false,
                  corner, tr, slew, limit, slack);
  slack_limit_ratio = slack / limit;
  if (slack_limit_ratio < hold_slack_limit_ratio_max_)
    return false;

  resizer_->checkLoadSlews(drvr_pin, 0.0, slew, limit, slack, corner);
  slack_limit_ratio = slack / limit;
  if (slack_limit_ratio < hold_slack_limit_ratio_max_)
    return false;

  return true;
}

} // namespace
