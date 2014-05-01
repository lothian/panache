#include <iostream>
#include "Testing.h"

using namespace panache::testing;
using namespace std;

int main(void)
{
    try {

        TestInfo h2o("Water, 6-31G**", "/home/ben/programming/psi4/libpanache/testfiles/h2o-631gss/");
        h2o.TestMoleculeConversion();
        h2o.PrintResults(cout);
    }

    catch(const exception & ex)
    {
        cout << "\nCaught exception! what() = " << ex.what() << "\n\n";
    }
    
    return 0;
}
