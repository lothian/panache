#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <array>
#include <memory>
#include <utility>
#include <cmath>

#include "panache/DFTensor.h"
#include "panache/SimpleMatrix.h"
#include "panache/Output.h"
#include "panache/c_convert.h" // int_t comes in through here
#include "panache/Flags.h"

#define QSO_ELEMENT_THRESHOLD 1e-11
#define QSO_SUM_THRESHOLD 1e-8
#define QSO_CHECKSUM_THRESHOLD 1.0

#define QMO_ELEMENT_THRESHOLD 1e-10
#define QMO_SUM_THRESHOLD 1e-6
#define QMO_CHECKSUM_THRESHOLD 2.0

using namespace panache;
using namespace std;

void PrintUsage(void)
{
    cout << "\n"
         << "Libpanache Testing Utility\n"
         << "\n"
         << "Usage: runtest [opt] <dir>\n"
         << "\n"
         << "Options:\n"
         << "-v           Verbose printing\n"
         << "-d           Write Qso to disk (rather than in core)\n"
         << "-b           Get Qso/Qmo in batches\n"
         << "-t           Use transpose of C matrix\n"
         << "-h           Print help (you're looking at it\n"
         << "<dir>        Directory holding the test information\n"
         << "\n\n";
}


struct TestMatrixInfo
{
    int nrow, ncol;
    double sum;
    double checksum;

    // index is first element of pair, value is second
    array<pair<int, double>, 100> elements;
};




// Note - exceptions are turned on for the ifstream object
// so that any parsing errors just throw an exeption. Catch those,
// and throw an exception
shared_ptr<BasisSet> ReadBasisFile(shared_ptr<Molecule> mol, const string & filename)
{
    ifstream f(filename.c_str());

    if(!f.is_open())
        throw std::runtime_error("Cannot open basis file!");

    f.exceptions(std::ifstream::failbit |
                 std::ifstream::badbit  |
                 std::ifstream::eofbit);

    try
    {
        int_t ncenters, nshells, nbf, nao, nprim;
        f >> nshells >> nbf >> nao >> nprim >> ncenters;

        unique_ptr<int_t[]> nshellspercenter(new int_t[ncenters]);
        unique_ptr<C_ShellInfo[]> shells(new C_ShellInfo[nshells]);

        for(int i = 0; i < ncenters; i++)
            f >> nshellspercenter[i];

        int dum; // holds the shell index. Not needed

        for(int i = 0; i < nshells; i++)
        {
            f >> dum >> shells[i].nprim >> shells[i].am >> shells[i].ispure;
            shells[i].coef = new double[shells[i].nprim];
            shells[i].exp = new double[shells[i].nprim];

            for(int j = 0; j < shells[i].nprim; j++)
                f >> shells[i].coef[j] >> shells[i].exp[j];
        }


        // Create the basis set
        // Last argument - these are not normalized
        auto ret = BasisSetFromArrays(mol, ncenters, nshellspercenter.get(), shells.get(), false);

        for(int i = 0; i < nshells; i++)
        {
            delete [] shells[i].exp;
            delete [] shells[i].coef;
        }

        return ret;
    }
    catch(...)
    {
        throw std::runtime_error("Error parsing basis file");
    }
}




shared_ptr<Molecule> ReadMoleculeFile(const string & filename)
{
    ifstream f(filename.c_str());

    if(!f.is_open())
        throw runtime_error("Cannot open file!");

    f.exceptions(std::ifstream::failbit |
                 std::ifstream::badbit  |
                 std::ifstream::eofbit);
    try
    {
        int_t natoms, nallatoms;
        std::string schoen, fullpg; // fullpg is no longer used

        f >> natoms >> nallatoms;
        f.ignore(); // remove newline

        getline(f, schoen);
        getline(f, fullpg);

        unique_ptr<C_AtomCenter[]> atoms(new C_AtomCenter[nallatoms]);

        double dummy; // for mass and Z-number

        for(int i = 0; i < nallatoms; i++)
        {
            char * s = new char[8];
            f.get(s, 4, ' ');
            atoms[i].symbol = s;

            f >> dummy  // Z is not used
              >> atoms[i].center[0]
              >> atoms[i].center[1]
              >> atoms[i].center[2]
              >> dummy;   // mass is not used

            f.ignore(); // ignore the newline
        }

        auto ret = MoleculeFromArrays(nallatoms, atoms.get());

        for(int i = 0; i < nallatoms; i++)
            delete [] atoms[i].symbol;

        return ret;
    }
    catch(...)
    {
        throw std::runtime_error("Error parsing molecule file");
    }
}



/*! \brief Prints a row in the results table (a single test)
 *
 * \param [in] out Stream to print to
 * \param [in] name Descriptive name of the test
 * \param [in] ref  The reference (expected) value
 * \param [in] run  The value for this run
 * \param [in] diff The absolute difference
 * \param [in] thresh Threshold for failing the test
 * \param [in] result String of the result (usually "pass" or "FAIL")
 */
template<typename T>
static void PrintRow(const std::string name,
                     const T & ref,
                     const T & run,
                     const T & diff,
                     const T & thresh,
                     const std::string & result)
{
    cout << std::setw(35) << std::left << name;
    cout << std::setw(15) << std::left << ref;
    cout << std::setw(15) << std::left << run;
    cout << std::setw(15) << std::left << diff;
    cout << std::setw(15) << std::left << thresh;
    cout << std::setw(15) << std::left << result << "\n";
}




/*! \brief Overload for printing strings (usually header rows)
 *
 * Used to prevent ambiguities when compiling
 *
 * \param [in] name Descriptive name of the test
 * \param [in] ref  The reference (expected) value
 * \param [in] run  The value for this run
 * \param [in] diff The absolute difference
 * \param [in] thresh Threshold for failing the test
 * \param [in] result String of the result (usually "pass" or "FAIL")
 */
static void PrintRow(const char * name,
                     const char * ref,
                     const char * run,
                     const char * diff,
                     const char * thresh,
                     const char * result)
{
    cout << std::setw(35) << std::left << name;
    cout << std::setw(15) << std::left << ref;
    cout << std::setw(15) << std::left << run;
    cout << std::setw(15) << std::left << diff;
    cout << std::setw(15) << std::left << thresh;
    cout << std::setw(15) << std::left << result << "\n";
}


/*! \brief Prints a line of 104 dashes
 *
 * \param [in] out Stream to print to
 */
static void PrintSeparator(void)
{
    cout << std::string(104, '-') << std::endl;
}



TestMatrixInfo ReadMatrixInfo(const string & filename)
{
    ifstream f(filename.c_str());

    if(!f.is_open())
        throw runtime_error("Cannot open matrix file!");

    f.exceptions(std::ifstream::failbit |
                 std::ifstream::badbit  |
                 std::ifstream::eofbit);
    try
    {
        TestMatrixInfo tmi;

        f >> tmi.nrow >> tmi.ncol >> tmi.sum >> tmi.checksum;

        for(int i = 0; i < 100; i++)
            f >> tmi.elements[i].first >> tmi.elements[i].second;

        return tmi;
    }
    catch(...)
    {
        throw runtime_error("Error parsing matrix file");
    }
}


template<typename T> int TestAndPrint(const string & title, T val, T ref, T threshold, bool alwaysprint)
{
    bool ok = true;

    T diff = std::abs(val - ref);
    if(diff > threshold)
        ok = false;

    if(alwaysprint || !ok)
        PrintRow(title, ref, val, diff, threshold, (ok ? "pass" : "FAIL"));

    // return 1 if it fails
    return (ok ? 0 : 1);
}

int TestAndPrint(const string & title, const string & val, string & ref, bool alwaysprint)
{
    bool ok = true;

    if(val != ref)
        ok = false;

    if(alwaysprint || !ok)
        PrintRow(title, ref, val, string(ok ? "-" : "Y"), string("-"), (ok ? "pass" : "FAIL"));

    // return 1 if it fails
    return (ok ? 0 : 1);
}



int TestMatrix(const string & title, const string & reffile,
               double * mat, int matsize,
               double sum_threshold, double checksum_threshold, double element_threshold,
               bool verbose)
{
    TestMatrixInfo tmi = ReadMatrixInfo(reffile);
    int nfailures = 0;
    cout << "***********************************************************************\n";
    cout << "Matrix \"" << title;
    cout << "\n";
    cout << "***********************************************************************\n";

    PrintRow("Name", "Reference", "This run", "Diff", "Threshold", "Pass/Fail");
    PrintSeparator();


    nfailures += TestAndPrint("# of elements", matsize, tmi.nrow*tmi.ncol, 0, verbose);

    // DON'T TEST ELEMENTS IF DIMENSIONS ARE WRONG
    if(nfailures > 0)
    {
        cout << "... not continuing with matrix test since dimensions are wrong!\n";
        return nfailures;
    }

    double sum = 0;
    double checksum = 0;

    for(int i = 0; i < matsize; i++)
    {
        sum += mat[i];
        checksum += mat[i]*static_cast<double>(i+1);
    }

    nfailures += TestAndPrint("sum", sum, tmi.sum, sum_threshold, verbose);
    nfailures += TestAndPrint("checksums", checksum, tmi.checksum, checksum_threshold, verbose);

    for(int i = 0; i < 100; i++)
    {
        stringstream ss;
        ss << "Element " << tmi.elements[i].first;

        nfailures += TestAndPrint(ss.str(), mat[tmi.elements[i].first], tmi.elements[i].second,
                                  element_threshold, verbose);
    }

    if(!verbose && nfailures == 0)
        cout << "           ... no failed tests!\n";
    cout << "\n";
    cout << "***********************************************************************\n";
    cout << "Matrix \"" << title << "\" result: " << (nfailures ? "FAIL" : "PASS");

    if(nfailures)
        cout << " (" << nfailures << " failures)";

    cout << "\n";
    cout << "***********************************************************************\n";
    cout << "\n\n\n\n\n";
    return nfailures;
}


std::shared_ptr<SimpleMatrix> ReadCMatrixFile(const string & filename)
{
    ifstream f(filename.c_str());

    if(!f.is_open())
        throw runtime_error("Cannot open matrix file!");

    f.exceptions(std::ifstream::failbit |
                 std::ifstream::badbit  |
                 std::ifstream::eofbit);
    try
    {
        int nrow, ncol;

        f >> nrow >> ncol;

        std::shared_ptr<SimpleMatrix> mat(new SimpleMatrix(nrow, ncol));

        for(int i = 0; i < nrow; i++)
        for(int j = 0; j < ncol; j++)
            f >> (*mat)(i,j);

        return mat;
    }
    catch(...)
    {
        throw runtime_error("Error parsing matrix file");
    }
}


int ReadNocc(const string & filename)
{
    ifstream f(filename.c_str());

    if(!f.is_open())
        throw runtime_error("Cannot open nocc file!");

    f.exceptions(std::ifstream::failbit |
                 std::ifstream::badbit  |
                 std::ifstream::eofbit);
    try
    {
        int nocc;

        f >> nocc;

        return nocc;
    }
    catch(...)
    {
        throw runtime_error("Error parsing nocc file");
    }
}



class IJIterator
{
private:
    int i_;
    int j_;
    int ij_;
    int ni_, nj_;
    int nij_;
    bool valid_;
    bool packed_;

    void validate_(void)
    {
        valid_ = (ij_ < nij_ && ij_ >= 0);
    }

public:
    IJIterator(int ni, int nj, bool packed = false) 
             : ni_(ni),nj_(nj),nij_(packed ? (ni_*(ni_+1))/2 : ni_*nj_),packed_(packed)
    { 
        i_ = j_ = ij_  = 0; 
        valid_ = true;
    }
    
    IJIterator(IJIterator & iji)
    {
        i_ = iji.i_;
        j_ = iji.j_;
        ij_ = iji.ij_;
        ni_ = iji.ni_;
        nj_ = iji.nj_;
        valid_ = iji.valid_;
        packed_ = packed_;
    }

    int i(void) const { return i_; }
    int j(void) const { return j_; }
    int ij(void) const { return ij_; }
    bool packed(void) const { return packed_; }
    operator bool() const { return valid_; }

    // prefix
    IJIterator & operator++()
    {
        return (*this += 1);
    }

    IJIterator & operator--()
    {
        return (*this -= 1);
    }


    // postfix
    IJIterator operator++(int)
    {
        IJIterator n(*this);
        ++(*this);
        return n; 
    }

    IJIterator operator--(int)
    {
        IJIterator n(*this);
        --(*this);
        return n; 
    }

    IJIterator & operator+=(int ij)
    {
        if(!valid_)
            return *this;

        if(packed_)
        {
            while(ij > 0)
            {
                j_++;
                if(j_ > i_)
                {
                    i_++;
                    j_ = 0;
                }
                ij--;
                ij_++;
            }
        }
        else
        {
            i_ += (j_ + ij)/nj_;
            j_ = (j_ + ij) % nj_;
            ij_ += ij;
        }

        validate_();

        return *this;
    }

    IJIterator & operator-=(int ij)
    {
        if(!valid_)
            return *this;

        if(packed_)
        {
            while(ij > 0)
            {
                ij_--;
                j_--;
                if(j_ < 0)
                {
                    i_--;
                    j_ = i_;

                    if(i_ < 0)
                        break;
                }
                ij--;
            }
        }
        else
        {
            int idec = ij / nj_;
            int jdec = ij % nj_;

            if(jdec > j_)
            {
                idec++;
                j_ = nj_;
            }

            i_ -= idec;
            j_ -= jdec;
            ij_ -= ij;
        }

        validate_();
        return *this;
    }


    IJIterator operator+(int ij)
    {
        IJIterator n(*this);
        n += ij;
        return n;
    }

    IJIterator operator-(int ij)
    {
        IJIterator n(*this);
        n -= ij;
        return n;
    }
};


int RunTestMatrix(DFTensor & dft, const string & title,
                  int batchsize, int tensorflag,
                  const string & reffile,
                  double sum_threshold, double checksum_threshold, double element_threshold,
                  bool verbose)
{
        int ret = 0;
        int naux, ndim1, ndim2;
        dft.TensorDimensions(tensorflag, naux, ndim1, ndim2);

        // may be packed
        int ndim12 = dft.QBatchSize(tensorflag);

        int matsize = naux * ndim1 * ndim2; // always expanded
        int bufsize = ndim12 * naux;
        if(batchsize)
            bufsize = ndim12 * batchsize;
            
        unique_ptr<double[]> mat(new double[matsize]);
        unique_ptr<double[]> outbuf(new double[bufsize]);
        std::fill(mat.get(), mat.get()+matsize, 0.0);
        std::fill(outbuf.get(), outbuf.get()+bufsize, 0.0);


        int n;
        int curq = 0;


        // First, do by q
        while((n = dft.GetQBatch(tensorflag, outbuf.get(), bufsize, curq)))
        {
            if(dft.IsPacked(tensorflag))
            {
                // expand
                for(int q = 0; q < n; q++)
                for(int i = 0; i < ndim1; i++)
                for(int j = 0; j <= i; j++)
                    mat[(curq+q)*ndim1*ndim2+i*ndim2+j] 
                  = mat[(curq+q)*ndim1*ndim2+j*ndim1+i] 
                  = outbuf[q*ndim12+i*(i+1)/2+j];
            }
            else
                std::copy(outbuf.get(), outbuf.get() + n*ndim12, mat.get() + curq*ndim12);

            curq += n;
        }


        string titleq(title);
        titleq.append(" (by Q)");
        ret += TestMatrix(titleq, reffile,
                          mat.get(), matsize,
                          sum_threshold, checksum_threshold, element_threshold,
                          verbose);


        // Now do by ij
        bufsize = matsize;
        if(batchsize)
            bufsize = naux * batchsize;

        outbuf = unique_ptr<double[]>(new double[bufsize]);
        std::fill(mat.get(), mat.get()+matsize, 0.0);
        std::fill(outbuf.get(), outbuf.get()+bufsize, 0.0);

        IJIterator ij(ndim1, ndim2, dft.IsPacked(tensorflag));

        while((n = dft.GetBatch(tensorflag, outbuf.get(), bufsize, ij.ij())))
        {
            // matrix testing is done by q
            for(int ni = 0; ni < n; ni++)
            {
                if(dft.IsPacked(tensorflag))
                {
                    // expand
                    for(int q = 0; q < naux; q++)
                        mat[q*ndim1*ndim2 + ij.i() * ndim2 + ij.j()]
                      = mat[q*ndim1*ndim2 + ij.j() * ndim1 + ij.i()]
                      = outbuf[ni*naux+q];
                }
                else
                {
                    for(int q = 0; q < naux; q++)
                        mat[q*ndim1*ndim2 + ij.i() * ndim2 + ij.j()] = outbuf[ni*naux+q];
                }

                ++ij;
            }
        }

        string titleij(title);
        titleij.append(" (by IJ)");
        ret += TestMatrix(titleij, reffile,
                          mat.get(), matsize,
                          sum_threshold, checksum_threshold, element_threshold,
                          verbose);

        return ret;

}


int main(int argc, char ** argv)
{
    int ret = 0;

    try
    {

        if(argc == 1)
        {
            PrintUsage();
            return 0;
        }

        string dir;

        bool verbose = false;
        bool inmem = true;
        bool byq = false;
        bool transpose = false;
        int batchsize = 0;

        for(int i = 1; i < argc; i++)
        {
            string starg(argv[i]);
            if(starg == "-h")
            {
                PrintUsage();
                return 0;
            }
            else if(starg == "-b")
                batchsize = 3;
            else if(starg == "-d")
                inmem = false;
            else if(starg == "-v")
                verbose = true;
            else if(starg == "-t")
                transpose = true;
            else if(starg == "-q")
                byq = true;
            else
            {
                // add trailing slash if needed
                if(starg[starg.length()-1] != '/')
                    starg.append("/");
                dir = starg;
            }
        }

        if(verbose)
            panache::output::SetOutput(&cout);

        // Get the test description
        string desc_path(dir);
        desc_path.append("desc");

        fstream desc(desc_path.c_str());
        if(!desc.is_open())
        {
            cout << "\nFatal Error: Cannot open " << desc_path << "\"\n\n";
            return -1;
        }

        string desc_str;
        getline(desc, desc_str);

        cout << "--------------------------------------\n";
        cout << "Results for test: " << desc_str << "\n";
        cout << "--------------------------------------\n";

        string primary_basis_filename(dir);
        string aux_basis_filename(dir);
        string molecule_filename(dir);
        string nocc_filename(dir);
        string qso_filename(dir);
        string qov_filename(dir);
        string qoo_filename(dir);
        string qvv_filename(dir);
        string qmo_filename(dir);
        string cmo_filename(dir);

        primary_basis_filename.append("basis.primary");
        aux_basis_filename.append("basis.aux");
        molecule_filename.append("geometry");
        nocc_filename.append("nocc");
        qso_filename.append("qso");
        qmo_filename.append("qmo");
        qov_filename.append("qov");
        qoo_filename.append("qoo");
        qvv_filename.append("qvv");
        cmo_filename.append("cmat");

        auto mol = ReadMoleculeFile(molecule_filename);
        auto primary = ReadBasisFile(mol, primary_basis_filename);
        auto aux = ReadBasisFile(mol, aux_basis_filename);
        auto cmat = ReadCMatrixFile(cmo_filename);

        if(transpose)
        {
            std::shared_ptr<SimpleMatrix> cmatt(new SimpleMatrix(cmat->ncol(), cmat->nrow()));
            for(size_t i = 0; i < cmat->nrow(); i++)
            for(size_t j = 0; j < cmat->ncol(); j++)
                (*cmatt)(j,i) = (*cmat)(i,j);

            std::swap(cmatt, cmat);
            // original cmat (now in cmatt), will be destructed here
        }

        int nso = primary->nbf();
        int nocc = ReadNocc(nocc_filename);
        int nmo = nso;

        DFTensor dft(primary, aux, "/tmp", 0);
        dft.SetCMatrix(cmat->pointer(), nmo, transpose);
        dft.SetNOcc(nocc);

        int qflags = (QGEN_QMO | QGEN_QOO | QGEN_QOV | QGEN_QVV);

        int qstore = 0;
        if(byq)
            qstore |= QSTORAGE_BYQ;


        if(inmem)
            dft.GenQTensors(qflags, qstore | QSTORAGE_INMEM);
        else
            dft.GenQTensors(qflags, qstore | QSTORAGE_ONDISK);




        ///////////
        // Test Qso
        ///////////
        ret += RunTestMatrix(dft, "QSO",
                             batchsize, QGEN_QSO,
                             qso_filename, 
                             QSO_SUM_THRESHOLD, QSO_CHECKSUM_THRESHOLD, QSO_ELEMENT_THRESHOLD,
                             verbose);

        ///////////
        // Test Qmo
        ///////////
        ret += RunTestMatrix(dft, "QMO",
                             batchsize, QGEN_QMO,
                             qmo_filename, 
                             QMO_SUM_THRESHOLD, QMO_CHECKSUM_THRESHOLD, QMO_ELEMENT_THRESHOLD,
                             verbose);

        ///////////
        // Test Qoo
        ///////////
        ret += RunTestMatrix(dft, "QOO",
                             batchsize, QGEN_QOO,
                             qoo_filename, 
                             QMO_SUM_THRESHOLD, QMO_CHECKSUM_THRESHOLD, QMO_ELEMENT_THRESHOLD,
                             verbose);

        ///////////
        // Test Qov
        ///////////
        ret += RunTestMatrix(dft, "QOV",
                             batchsize, QGEN_QOV,
                             qov_filename, 
                             QMO_SUM_THRESHOLD, QMO_CHECKSUM_THRESHOLD, QMO_ELEMENT_THRESHOLD,
                             verbose);

        ///////////
        // Test Qvv
        ///////////
        ret += RunTestMatrix(dft, "QVV",
                             batchsize, QGEN_QVV,
                             qvv_filename, 
                             QMO_SUM_THRESHOLD, QMO_CHECKSUM_THRESHOLD, QMO_ELEMENT_THRESHOLD,
                             verbose);


        cout << "\n\n";
        cout << "*************************************************\n"
             << "*************************************************\n"
             << "OVERALL RESULT: " << (ret ? "FAIL" : "PASS") << "\n";
        if(ret)
            cout << "    ( " << ret << " failures)\n";
        cout << "*************************************************\n"
             << "*************************************************\n";


        dft.PrintTimings();


    }

    catch(const exception & ex)
    {
        cout << "\n*****************"
             << "\nCAUGHT EXCEPTION!"
             << "\n" << ex.what()
             << "\n*****************"
             << "\n\n";

        ret = -1;
    }

    return ret;
}





