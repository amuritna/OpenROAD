///////////////////////////////////////////////////////////////////////////////
// BSD 3-Clause License
//
// Copyright (c) 2021, The Regents of the University of California
// All rights reserved.
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
// ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include <vector>

#include "SimulatedAnnealingCore.h"
#include "object.h"

namespace utl {
class Logger;
}

namespace mpl {

// Class SimulatedAnnealingCore is a base class
// It will have two derived classes:
// 1) SACoreHardMacro : SA for hard macros.  It will be called by ShapeEngine
// and PinAlignEngine 2) SACoreSoftMacro : SA for soft macros.  It will be
// called by MacroPlaceEngine
class SACoreSoftMacro : public SimulatedAnnealingCore<SoftMacro>
{
 public:
  SACoreSoftMacro(){};
  SACoreSoftMacro(float outline_width,
                  float outline_height,  // boundary constraints
                  const std::vector<SoftMacro>& macros,
                  // weight for different penalty
                  float area_weight,
                  float outline_weight,
                  float wirelength_weight,
                  float guidance_weight,
                  float fence_weight,  // each blockage will be modeled by a
                                       // macro with fences
                  float boundary_weight,
                  float macro_blockage_weight,
                  float notch_weight,
                  // notch threshold
                  float notch_h_threshold,
                  float notch_v_threshold,
                  // action prob
                  float pos_swap_prob,
                  float neg_swap_prob,
                  float double_swap_prob,
                  float exchange_prob,
                  float resize_prob,
                  // Fast SA hyperparameter
                  float init_prob,
                  int max_num_step,
                  int num_perturb_per_step,
                  int k,
                  int c,
                  unsigned seed = 0,
                  utl::Logger* logger = nullptr);
  // accessors
  float GetBoundaryPenalty() const;
  float GetNormBoundaryPenalty() const;
  float GetNotchPenalty() const;
  float GetNormNotchPenalty() const;
  float GetMacroBlockagePenalty() const;
  float GetNormMacroBlockagePenalty() const;

  void PrintResults() const;  // just for test

  // Initialize the SA worker
  void Initialize();
  // adjust the size of MixedCluster to fill the empty space
  void FillDeadSpace();
  void AlignMacroClusters();
  void SetBlockages(const std::vector<Rect>& blockages)
  {
    blockages_ = blockages;
  }

 private:
  float CalNormCost();
  void CalPenalty();

  void Perturb();
  void Restore();
  // actions used
  void Resize();

  void Shrink();

  std::vector<Rect> blockages_;

  // A utility function for FillDeadSpace.
  // It's used for calculate the start point and end point for a segment in a
  // grid
  void CalSegmentLoc(float seg_start,
                     float seg_end,
                     int& start_id,
                     int& end_id,
                     std::vector<float>& grid);

  // notch threshold
  float notch_h_th_;
  float notch_v_th_;

  float adjust_h_th_;  // the threshold for adjust hard macro clusters
                       // horizontally
  float adjust_v_th_;  // the threshold for adjust hard macro clusters
                       // vertically

  // additional penalties
  float boundary_weight_ = 0.0;
  float macro_blockage_weight_ = 0.0;

  float boundary_penalty_ = 0.0;
  float notch_penalty_ = 0.0;
  float macro_blockage_penalty_ = 0.0;

  float pre_boundary_penalty_ = 0.0;
  float pre_notch_penalty_ = 0.0;
  float pre_macro_blockage_penalty_ = 0.0;

  float norm_boundary_penalty_ = 0.0;
  float norm_notch_penalty_ = 0.0;
  float norm_macro_blockage_penalty_ = 0.0;

  void CalBoundaryPenalty();
  void CalNotchPenalty();
  void CalMacroBlockagePenalty();

  // action prob
  float resize_prob_ = 0.0;
};

}  // namespace mpl
