/*
 *@BEGIN LICENSE
 *
 * PSI4: an ab initio quantum chemistry software package
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 *@END LICENSE
 */

//
// fjt.h
//
// Copyright (C) 2001 Edward Valeev
//
// Author: Edward Valeev <evaleev@vt.edu>
// Maintainer: EV
//
// This file is part of the SC Toolkit.
//
// The SC Toolkit is free software; you can redistribute it and/or modify
// it under the terms of the GNU Library General Public License as published by
// the Free Software Foundation; either version 2, or (at your option)
// any later version.
//
// The SC Toolkit is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Library General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
//
// The U.S. Government is granted a limited license as per AL 91-7.
//

#ifndef PANACHE_FJT_H
#define PANACHE_FJT_H

#include <memory>
#include "panache/SimpleMatrix.h"

namespace panache {

class CorrelationFactor;
typedef std::shared_ptr<CorrelationFactor> SharedCorrelationFactor;

/// Evaluates the Boys function F_j(T)
class Fjt {
public:
    Fjt();
    virtual ~Fjt();
    /** Computed F_j(T) for every 0 <= j <= J (total of J+1 doubles).
        The user may read/write these values.
        The values will be overwritten with the next call to this functions.
        The pointer will be invalidated after the call to ~Fjt. */
    virtual double *values(int J, double T) =0;
    virtual void set_rho(double /*rho*/) { }
};

#define TAYLOR_INTERPOLATION_ORDER 6
#define TAYLOR_INTERPOLATION_AND_RECURSION 0  // compute F_lmax(T) and then iterate down to F_0(T)? Else use interpolation only
/// Uses Taylor interpolation of up to 8-th order to compute the Boys function
class Taylor_Fjt : public Fjt {
    static double relative_zero_;
public:
    static const int max_interp_order = 8;

    Taylor_Fjt(unsigned int jmax, double accuracy);
    virtual ~Taylor_Fjt();
    /// Implements Fjt::values()
    double *values(int J, double T);
private:
    SimpleMatrix grid_;        /* Table of "exact" Fm(T) values. Row index corresponds to
                                  values of T (max_T+1 rows), column index to values
                                  of m (max_m+1 columns) */
    double delT_;              /* The step size for T, depends on cutoff */
    double oodelT_;            /* 1.0 / delT_, see above */
    double cutoff_;            /* Tolerance cutoff used in all computations of Fm(T) */
    int interp_order_;         /* Order of (Taylor) interpolation */
    int max_m_;                /* Maximum value of m in the table, depends on cutoff
                                  and the number of terms in Taylor interpolation */
    int max_T_;                /* Maximum index of T in the table, depends on cutoff
                                  and m */
    double *T_crit_;           /* Maximum T for each row, depends on cutoff;
                                  for a given m and T_idx <= max_T_idx[m] use Taylor interpolation,
                                  for a given m and T_idx > max_T_idx[m] use the asymptotic formula */
    double *F_;                /* Here computed values of Fj(T) are stored */
};

/// "Old" intv3 code from Curt
/// Computes F_j(T) using 6-th order Taylor interpolation
class FJT: public Fjt {
private:
    double **gtable;

    int maxj;
    double *denomarray;
    double wval_infinity;
    int itable_infinity;

    double *int_fjttable;

    int ngtable() const { return maxj + 7; }
public:
    FJT(int n);
    virtual ~FJT();
    /// implementation of Fjt::values()
    double *values(int J, double T);
};

class GaussianFundamental : public Fjt {
protected:
    SharedCorrelationFactor cf_;
    double rho_;
    double* value_;

public:
    GaussianFundamental(SharedCorrelationFactor cf, int max);
    virtual ~GaussianFundamental();

    virtual double* values(int J, double T) = 0;
    void set_rho(double rho);
};

class ErfFundamental : public GaussianFundamental {
private:
    double omega_;
    std::shared_ptr<FJT> boys_;
public:
    ErfFundamental(double omega, int max);
    virtual ~ErfFundamental();
    double* values(int J, double T);
    void setOmega(double omega) { omega_ = omega; }
};

} // end of namespace panache

#endif //PANACHE_FJT_H
