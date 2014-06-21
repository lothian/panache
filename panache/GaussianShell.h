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

#ifndef PANACHE_GAUSSIANSHELL_H
#define PANACHE_GAUSSIANSHELL_H

#include "ShellInfo.h"

namespace panache {

/*! \ingroup MINTS
 *  \class GaussianShell
 *  \brief Gaussian orbital shell.
 */
class GaussianShell
{
private:
    /// Angular momentum
    int l_;
    /// Flag for pure angular momentum
    int puream_;
    /// Exponents (of length nprimitives_)
    const double* exp_;
    /// Original (un-normalized) contraction coefficients (of length nprimitives)
    const double* original_coef_;
    /// Contraction coefficients (of length nprimitives_)
    const double* coef_;

    /// Atom number this shell goes to. Needed when indexing integral derivatives.
    int nc_;
    /// Atomic coordinates of this center
    const double *center_;
    /// First basis function in this shell
    int start_;

    /// How many cartesian functions? (1=s, 3=p, 6=d, ...)
    int ncartesian_;
    /// The number of primitives in this shell
    int nprimitive_;
    /** How many functions? (1=s, 3=p, 5/6=d, ...)
     * Dependent on the value of puream_
     */
    int nfunction_;

    /** Normalizes a single primitive.
     *  @param p The primitive index to normalize.
     *  @return Normalization constant to be applied to the primitive.
     */
    double primitive_normalization(int p);
    /** Normalizes an entire contraction set. Applies the normalization to the coefficients
     *  @param gs The contraction set to normalize.
     */
    void contraction_normalization();

    /** Lookup array that when you index the angular momentum it returns the lowercase letter corresponding to it. */
    static const char *amtypes;
    /** Lookup array that when you index the angular momentum it returns the uppercase letter corresponding to it. */
    static const char *AMTYPES;

public:
    /** Constructor.
     *  @param am Angular momentum.
     *  @param pure Pure spherical harmonics, or Cartesian.
     *  @param oc An array of contraction coefficients.
     *  @param c An array of normalized contraction coefficients.
     *  @param e An array of exponent values.
     *  @param pure an enum describing whether this shell uses pure or Cartesian functions.
     *  @param nc The atomic center that this shell is located on. Must map back to the correct atom in the owning BasisSet molecule_. Used in integral derivatives for indexing.
     *  @param center The x, y, z position of the shell. This is passed to reduce the number of calls to the molecule.
     *  @param start The starting index of the first function this shell provides. Used to provide starting positions in matrices.
     *  @param pt Is the shell already normalized?
     */
    GaussianShell(int am,
                  int nprimitive,
                  const double *oc,
                  const double *c,
                  const double *e,
                  ShellInfo::GaussianType pure,
                  int nc,
                  const double* center,
                  int start);

    ///Builds and empty GShell
    GaussianShell() {}

    /// The number of primitive Gaussians
    int nprimitive() const;
    /// Total number of basis functions
    int nfunction() const;
    /// Total number of functions if this shell was Cartesian
    int ncartesian() const          { return ncartesian_; }
    /// The angular momentum of the given contraction
    int am() const                  { return l_; }
    /// The character symbol for the angular momentum of the given contraction
    char amchar() const             { return amtypes[l_]; }
    /// The character symbol for the angular momentum of the given contraction (upper case)
    char AMCHAR() const             { return AMTYPES[l_]; }
    /// Returns true if contraction is Cartesian
    bool is_cartesian() const       { return !puream_; }
    /// Returns true if contraction is pure
    bool is_pure() const            { return puream_; }

    /// Returns the center of the Molecule this shell is on
    const double* center() const;
    /// Returns the atom number this shell is on. Used by integral derivatives for indexing.
    int ncenter() const             { return nc_; }

    /// Returns the exponent of the given primitive
    double exp(int prim) const      { return exp_[prim]; }
    /// Return coefficient of pi'th primitive
    double coef(int pi) const       { return coef_[pi]; }
    /// Return unnormalized coefficient of pi'th primitive
    double original_coef(int pi) const { return original_coef_[pi]; }
    /// Returns the exponents
    const double* exps() const { return exp_; }
    /// Return coefficients
    const double* coefs() const { return coef_; }
    /// Return unnormalized coefficients
    const double* original_coefs() const { return original_coef_; }

    /// Basis function index where this shell starts.
    int function_index() const      { return start_; }
    void set_function_index(int i)  { start_ = i; }

    /// Print
    void print(void) const;
};

}

#endif //PANACHE_GAUSSIANSHELL_H
