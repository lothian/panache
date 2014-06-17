#ifndef PANACHE_REORDER_H
#define PANACHE_REORDER_H

#include <vector>
#include <array>
#include <functional>

#ifndef MAX_REORDER_AM
#define MAX_REORDER_AM 10
#endif

using std::vector;
using std::array;
using std::function;


namespace panache {
namespace reorder {


struct PointerMap
{
    int start;
    vector<unsigned short> order;

    PointerMap(int start, const vector<unsigned short> & order)
        : start(start), order(order)
    { }

};



class Orderings
{
private:
    // we keep a vector so we can have a homogeneous container
    array<vector<unsigned short>, MAX_REORDER_AM > orderings_;

public:
    void SetOrder(int am, vector<unsigned short> order)
    {
        if(am > MAX_REORDER_AM)
            throw RuntimeError("Cannot set reordering for this AM - set MAX_REORDER_AM to a larger value");

        orderings_[am].assign(order.begin(), order.end());
    }


    vector<unsigned short> GetOrder(int am) const
    {
        if(am > MAX_REORDER_AM)
            throw RuntimeError("Cannot get reordering for this AM - set MAX_REORDER_AM to a larger value");

        return orderings_[am];
    }


    bool NeedsReordering(int am) const
    {
        if(am > MAX_REORDER_AM)
            throw RuntimeError("Cannot get reordering for this AM - set MAX_REORDER_AM to a larger value");

        return orderings_[am].size();
    }


};



class GAMESS_Ordering : public Orderings
{
public:
    GAMESS_Ordering(void)
    {
        // don't ask
        SetOrder(2, {1, 4, 6, 2, 3, 5} );
        SetOrder(3, {1, 7, 10, 2, 3, 4, 8, 6, 9, 5} );
        SetOrder(4, {1, 11, 15, 2, 3, 7, 12, 10, 14, 4, 6, 13, 5, 8, 9} );
        SetOrder(5, {1, 16, 21, 2, 3, 11, 17, 15, 20, 4, 6, 7, 18, 10, 19, 8, 9, 13, 9, 13, 9} );
        SetOrder(6, {1, 22, 28, 2, 3, 16, 23, 21, 27, 4, 6, 11, 24, 15, 26, 7, 10, 25, 8, 9, 12, 18, 14, 19, 13, 13, 13, 13} );
        SetOrder(7, {1, 29, 36, 2, 3, 22, 30, 28, 35, 4, 6, 16, 31, 21, 34, 7, 10, 11, 32, 15, 33, 12, 14, 25, 13, 18, 19, 18, 19, 18, 19, 18, 19, 18, 19, 18} );
        SetOrder(8, {1, 37, 45, 2, 3, 29, 38, 36, 44, 4, 6, 22, 39, 28, 43, 7, 10, 16, 40, 21, 42, 11, 15, 41, 12, 14, 17, 32, 20, 33, 18, 19, 25, 19, 25, 19, 25, 19, 25, 19, 25, 19, 25, 19, 25} );
    }
};



} } // close namespace panache::reorder



#endif