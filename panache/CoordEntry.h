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

#ifndef PANACHE_COORDENTRY_H
#define PANACHE_COORDENTRY_H

#include <map>
#include <vector>
#include <string>
#include <memory>
#include "Vector3.h"

#define CLEANUP_THRESH 1.0E-14

namespace panache {

class Vector3;


/**
 * An abstract class to handle storage of Cartesian coordinate values, which
 * may be defined in terms of other variables through this mechanism, greatly
 * simplifying Z-matrix specification, for example.
 */
class CoordValue
{
protected:
    /// Fixed coordinate?
    bool fixed_;
    /// Whether the current value is up to date or not
    bool computed_;
public:

    CoordValue() : fixed_(false), computed_(false)
    { }

    CoordValue(bool fixed) : fixed_(fixed), computed_(false)
    { }

    CoordValue(bool fixed, bool computed) : fixed_(fixed), computed_(computed)
    { }

    void set_fixed(bool fixed) { fixed_ = fixed; }
    bool fixed() const { return fixed_; }

    /**
     * The specialization of CoordValue used to represent this number.
     * NumberType: A simple number
     * VariableType: A number to be extracted from a map owned by molecule
     */
    enum CoordValueType {NumberType, VariableType};
    /// Computes the current value, and returns it.
    virtual double compute() =0;
    /// Sets the current value.
    virtual void set(double val) =0;
    /// The type of variable representation
    virtual CoordValueType type() =0;
    /// Flag the current value as outdated
    void invalidate() { computed_ = false; }
    /// Clones the current object, using a user-provided variable array, for deep copying
    virtual std::shared_ptr<CoordValue> clone( std::map<std::string, double>& map) =0;
};

/**
 * Specialization of CoordValue that is simply a number to be stored.
 */
class NumberValue : public CoordValue
{
    double value_;
public:
    NumberValue(double value, bool fixed = false) : CoordValue(fixed, true), value_(value) {}
    double compute() { return value_; }
    void set(double val) { if (!fixed_) value_ = val; }
    CoordValueType type() { return NumberType; }
    std::shared_ptr<CoordValue> clone(std::map<std::string, double>& map) {
        return std::shared_ptr<CoordValue>(new NumberValue(value_, fixed_));
    }
};

/**
 * Specialization of CoordValue, where the current value depends on the list of
 * geometry values stored by the molecule.
 */
class VariableValue : public CoordValue
{
    const std::string name_;
    std::map<std::string, double>& geometryVariables_;
    bool negate_;
public:
    VariableValue(const std::string name, std::map<std::string, double>& geometryVariables, bool negate=false, bool fixed = false)
        : CoordValue(fixed, true), name_(name), geometryVariables_(geometryVariables), negate_(negate) {}
    double compute();
    bool negated() const { return negate_; }
    const std::string & name() const { return name_; }
    void set(double val) { if (!fixed_) { geometryVariables_[name_] = negate_ ? -val : val; } }
    CoordValueType type() { return VariableType; }
    std::shared_ptr<CoordValue> clone(std::map<std::string, double>& map) {
        return std::shared_ptr<CoordValue>(new VariableValue(name_, map, negate_, fixed_));
    }
};


class CoordEntry
{
protected:
    int entry_number_;
    bool computed_;
    Vector3 coordinates_;

    /// Atomic number of the atom
    double Z_;
    double charge_;
    double mass_;

    /// Label of the atom minus any extra info (H1 => H)
    std::string symbol_;
    /// Original label from the molecule from the input file (H1)
    std::string label_;
    /// Is this a ghost atom?
    bool ghosted_;

    /// Different types of basis sets that can be assigned to this atom.
    //       option name, basis name
    std::map<std::string, std::string> basissets_;

    /// Computes the distance between two sets of coordinates
    static double r(const Vector3 &a1, const Vector3 &a2) { return a1.distance(a2); }
    /// Computes the angle (in rad.) between three sets of coordinates.
    static double a(const Vector3 &a1, const Vector3 &a2, const Vector3 &a3)
        { Vector3 eBA(a2-a1), eBC(a2-a3); eBA.normalize(); eBC.normalize();
          double costheta = eBA.dot(eBC);
          if(costheta > 1.0 - CLEANUP_THRESH)  costheta = 1.0;
          if(costheta < CLEANUP_THRESH - 1.0)  costheta = -1.0;
          return acos(costheta); }
    /// Computes the dihedral (in rad.) between four sets of coordinates.
    static double d(const Vector3 &a1, const Vector3 &a2, const Vector3 &a3, const Vector3 &a4)
        { Vector3 eBA(a2-a1), eDC(a4-a3), eCB(a3-a2);
          double CBNorm = eCB.norm();
          Vector3 DCxCB(eDC.cross(eCB));
          Vector3 CBxBA(eCB.cross(eBA));
          return -atan2(CBNorm * eDC.dot(eCB.cross(eBA)), DCxCB.dot(CBxBA));}
public:
    /**
     * The type of CoordEntry specialization
     * CartesianEntry: Cartesian storage.
     * ZMatrixEntry: ZMatrix storage.
     */
    enum CoordEntryType {CartesianCoord, ZMatrixCoord};
    CoordEntry(int entry_number, double Z, double charge, double mass, const std::string& symbol, const std::string& label="");
    CoordEntry(int entry_number, double Z, double charge, double mass, const std::string& symbol, const std::string& label, const std::map<std::string, std::string>& basis);
    virtual ~CoordEntry();

    /// Computes the values of the coordinates (in whichever units were inputted), returning them in a Vector.
    virtual const Vector3& compute() =0;
    /// Given the current set of coordinates, updates the values of this atom's coordinates,
    /// and any variables that may depend on it.
    virtual void set_coordinates(double x, double y, double z) = 0;
    /// The type of CoordEntry Specialization
    virtual CoordEntryType type() =0;
    /// Whether the current atom's coordinates are up-to-date.
    bool is_computed() const { return computed_; }
    /// Whether this atom has the same mass and basis sets as another atom
    bool is_equivalent_to(const std::shared_ptr<CoordEntry> &other) const;
    /// Flags the current coordinates as being outdated.
    virtual void invalidate() =0;
    /// Clones the current object, using a user-provided variable array, for deep copying
    virtual std::shared_ptr<CoordEntry> clone( std::vector<std::shared_ptr<CoordEntry> > &atoms, std::map<std::string, double>& map) =0;

    /// Whether the current atom is ghosted or not.
    const bool& is_ghosted() const { return ghosted_; }
    /// Flag the atom as either ghost or real.
    void set_ghosted(bool ghosted) { ghosted_ = ghosted; }

    /// The nuclear charge of the current atom (0 if ghosted).
    const double& Z() const;
    /// The "atomic charge" of the current atom (for SAD purposes).
    const double& charge() const { return charge_; }
    /// The atomic mass of the current atom.
    const double& mass() const { return mass_; }
    /// The atomic symbol.
    const std::string& symbol() const { return symbol_; }
    /// The atom label.
    const std::string& label() const { return label_; }
    /// The order in which this appears in the full atom list.
    const int& entry_number() const { return entry_number_; }

    /** Set the basis for this atom
     * @param type Keyword from input file, basis, ri_basis, etc.
     * @param name Value from input file
     */
    void set_basisset(const std::string& name, const std::string& type="BASIS");

    /** Returns the basis name for the provided type.
     * @param type Keyword from input file.
     * @returns the value from input.
     */
    const std::string& basisset(const std::string& type="BASIS") const;
    const std::map<std::string, std::string>& basissets() const { return basissets_;}

};

class CartesianEntry : public CoordEntry{
    std::shared_ptr<CoordValue> x_;
    std::shared_ptr<CoordValue> y_;
    std::shared_ptr<CoordValue> z_;
public:
    CartesianEntry(int entry_number, double Z, double charge, double mass, const std::string& symbol, const std::string& label,
                   std::shared_ptr<CoordValue> x, std::shared_ptr<CoordValue> y, std::shared_ptr<CoordValue> z);
    CartesianEntry(int entry_number, double Z, double charge, double mass, const std::string& symbol, const std::string& label,
                   std::shared_ptr<CoordValue> x, std::shared_ptr<CoordValue> y, std::shared_ptr<CoordValue> z, const std::map<std::string, std::string>& basis);

    const Vector3& compute();
    void set_coordinates(double x, double y, double z);
    CoordEntryType type() { return CartesianCoord; }
    void invalidate () { computed_ = false; x_->invalidate(); y_->invalidate(); z_->invalidate(); }
    std::shared_ptr<CoordEntry> clone( std::vector<std::shared_ptr<CoordEntry> > &atoms, std::map<std::string, double>& map){
        std::shared_ptr<CoordEntry> temp(new CartesianEntry(entry_number_, Z_, charge_, mass_, symbol_, label_, x_->clone(map), y_->clone(map), z_->clone(map), basissets_));
        return temp;
    }
};

class ZMatrixEntry : public CoordEntry
{
    std::shared_ptr<CoordEntry> rto_;
    std::shared_ptr<CoordValue> rval_;
    std::shared_ptr<CoordEntry> ato_;
    std::shared_ptr<CoordValue> aval_;
    std::shared_ptr<CoordEntry> dto_;
    std::shared_ptr<CoordValue> dval_;

public:
    ZMatrixEntry(int entry_number, double Z, double charge, double mass, const std::string& symbol, const std::string& label,
                 std::shared_ptr<CoordEntry> rto=std::shared_ptr<CoordEntry>(),
                 std::shared_ptr<CoordValue> rval=std::shared_ptr<CoordValue>(),
                 std::shared_ptr<CoordEntry> ato=std::shared_ptr<CoordEntry>(),
                 std::shared_ptr<CoordValue> aval=std::shared_ptr<CoordValue>(),
                 std::shared_ptr<CoordEntry> dto=std::shared_ptr<CoordEntry>(),
                 std::shared_ptr<CoordValue> dval=std::shared_ptr<CoordValue>());
    ZMatrixEntry(int entry_number, double Z, double charge, double mass, const std::string& symbol, const std::string& label,
                 const std::map<std::string, std::string>& basis,
                 std::shared_ptr<CoordEntry> rto=std::shared_ptr<CoordEntry>(),
                 std::shared_ptr<CoordValue> rval=std::shared_ptr<CoordValue>(),
                 std::shared_ptr<CoordEntry> ato=std::shared_ptr<CoordEntry>(),
                 std::shared_ptr<CoordValue> aval=std::shared_ptr<CoordValue>(),
                 std::shared_ptr<CoordEntry> dto=std::shared_ptr<CoordEntry>(),
                 std::shared_ptr<CoordValue> dval=std::shared_ptr<CoordValue>());

    virtual ~ZMatrixEntry();
    void invalidate () { computed_ = false; if(rval_ != 0) rval_->invalidate();
                                         if(aval_ != 0) aval_->invalidate();
                                         if(dval_ != 0) dval_->invalidate(); }
    const Vector3& compute();
    void print_in_input_format();
    std::string string_in_input_format();
    void set_coordinates(double x, double y, double z);
    CoordEntryType type() { return ZMatrixCoord; }
    std::shared_ptr<CoordEntry> clone( std::vector<std::shared_ptr<CoordEntry> > &atoms, std::map<std::string, double>& map){
        std::shared_ptr<CoordEntry> temp;
        if(rto_ == 0 && ato_ == 0 && dto_ == 0){
            temp = std::shared_ptr<CoordEntry>(new ZMatrixEntry(entry_number_, Z_, charge_, mass_, symbol_, label_, basissets_));
        }else if(ato_ == 0 && dto_ == 0){
            temp = std::shared_ptr<CoordEntry>(new ZMatrixEntry(entry_number_, Z_, charge_, mass_, symbol_, label_, basissets_,
                                          atoms[rto_->entry_number()], rval_->clone(map)));
        }else if(dto_ == 0){
            temp = std::shared_ptr<CoordEntry>(new ZMatrixEntry(entry_number_, Z_, charge_, mass_, symbol_, label_, basissets_,
                                          atoms[rto_->entry_number()], rval_->clone(map),
                                          atoms[ato_->entry_number()], aval_->clone(map)));
        }
        else {
            temp = std::shared_ptr<CoordEntry>(new ZMatrixEntry(entry_number_, Z_, charge_, mass_, symbol_, label_, basissets_,
                                      atoms[rto_->entry_number()], rval_->clone(map),
                                      atoms[ato_->entry_number()], aval_->clone(map),
                                      atoms[dto_->entry_number()], dval_->clone(map)));
        }
        return temp;
    }
};

}

#endif //PANACHE_COORDENTRY_H