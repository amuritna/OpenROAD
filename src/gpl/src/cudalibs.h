#ifndef __CUDALIBS__
#define __CUDALIBS__

#include <cuda_runtime.h>
#include "cusparse.h"
#include "cusolverSp.h"

#include <vector>
#include <Eigen/SparseCore>

// namespace utl {
// class Logger;
// }


void cusolverSpQR(std::vector<int>& cooRowIndex, std::vector<int>& cooColIndex, std::vector<float>& cooVal, Eigen::VectorXf& fixedInstForceVec, Eigen::VectorXf& instLocVec, int m, int nnz, float tol, int reorder, int singularity);

#endif