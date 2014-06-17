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

#ifndef PANACHE_SHELLROTATION_H
#define PANACHE_SHELLROTATION_H

namespace panache {

class SymmetryOperation;

class ShellRotation
{
    int n_;
    int am_;
    double **r_;

    void done();

public:
    /// Initialize this ShellRotation to hold a n by n transformation.
    ShellRotation(int n);
    /// Initialize this from another ShellRotation.
    ShellRotation(const ShellRotation&);
    /// Initialize using init(...) or, if pure is nonzero, init_pure(...).
    ShellRotation(int a, SymmetryOperation&, int pure=0);
    virtual ~ShellRotation();

    /// Assign this to another shell rotation.
    ShellRotation& operator=(const ShellRotation&);

    /** Initialize the ShellRotation for Cartesian functions, given the
        angular momentum, a symmetry operation*/
    void init(int a, SymmetryOperation&);
    /** Initialize the ShellRotation for solid harmonic function, given the
        angular momentum, a symmetry operation */
    void init_pure(int a, SymmetryOperation&);

    /// Return the angular momentum.
    int am() const { return am_; }
    /// Return the number of functions in a shell.
    int dim() const { return n_; }

    /// Return an element of the transform matrix.
    double& operator()(int i, int j) { return r_[i][j]; }
    /// Return a row of the transform matrix.
    double* operator[](int i) { return r_[i]; }

    /// Returns the result of rot*this.
    ShellRotation operate(const ShellRotation&rot) const;
    /// Returns the result of rot*this*transpose(rot).
    ShellRotation transform(const ShellRotation&rot) const;

    /// Return the trace of the transformation.
    double trace() const;

    /// Print the object.
    void print() const;
};

}

#endif //PANACHE_SHELLROTATION_H