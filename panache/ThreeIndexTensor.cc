/*! \file
 * \brief Generic three-index tensor generation and manipulation (source)
 * \author Benjamin Pritchard (ben@bennyp.org)
 */


#include "panache/ThreeIndexTensor.h"
#include "panache/storedqtensor/StoredQTensor.h"
#include "panache/storedqtensor/StoredQTensorFactory.h"
#include "panache/Molecule.h"
#include "panache/BasisSet.h"
#include "panache/Exception.h"
#include "panache/Lapack.h"
#include "panache/Output.h"

// for reordering
#include "panache/MemorySwapper.h"
#include "panache/Reorder.h"


#ifdef _OPENMP
#include <omp.h>
#endif

namespace panache
{

ThreeIndexTensor::ThreeIndexTensor(SharedBasisSet primary,
                     const std::string & directory,
                     int qtype,
                     int bsorder,
                     int nthreads)
    : primary_(primary), directory_(directory), qtype_(qtype), bsorder_(bsorder)
{
    //remove trailing slashes
    while(directory_.size() > 1 && directory_.back() == '/')
        directory_ = directory_.substr(0, directory_.size()-1);

    nmo_ = nmo2_ = nmotri_ = 0;
    nocc_ = nfroz_ = nvir_ = 0;

    nso_ = primary_->nbf();
    nso2_ = nso_*nso_;
    nsotri_ = (nso_*(nso_+1))/2;
    naux_ = 0; // must be set by derived classes

    SetNThread(nthreads);
}


int ThreeIndexTensor::SetNThread(int nthread)
{
#ifdef _OPENMP
    if(nthread <= 0)
        nthreads_ = omp_get_max_threads();
    else
        nthreads_ = nthread;
#else
    nthreads_ = 1;
#endif

    return nthreads_;
}



ThreeIndexTensor::~ThreeIndexTensor()
{
}

void ThreeIndexTensor::SetCMatrix(double * cmo, int nmo, bool cmo_is_trans)
{
    if(Cmo_)
        throw RuntimeError("Error - C matrix already set!");

    nmo_ = nmo;
    nmo2_ = nmo*nmo;
    nmotri_ = (nmo*(nmo+1))/2;

    Cmo_ = std::unique_ptr<double[]>(new double[nmo_*nso_]);

    if(cmo_is_trans)
    {
        for(int i = 0; i < nso_; i++)
            for(int j = 0; j < nmo_; j++)
                Cmo_[i*nmo_+j] = cmo[j*nso_+i];
    }
    else
        std::copy(cmo, cmo+(nmo_*nso_), Cmo_.get());

}


void ThreeIndexTensor::Delete(int qflags)
{
    if(qflags & QGEN_QSO)
        qso_.reset();

    if(qflags & QGEN_QMO)
        qmo_.reset();

    if(qflags & QGEN_QOO)
        qoo_.reset();

    if(qflags & QGEN_QOV)
        qov_.reset();

    if(qflags & QGEN_QVV)
        qvv_.reset();
}


void ThreeIndexTensor::GenQTensors(int qflags, int storeflags)
{
#ifdef PANACHE_TIMING
    Timer tim;
    tim.Start();
#endif

    // remove packed setting
    storeflags &= ~QSTORAGE_PACKED;

    if( (!Cmo_ || nocc_ == 0) && 
        ((qflags & QGEN_QMO) || 
         (qflags & QGEN_QOO) || 
         (qflags & QGEN_QOV) || 
         (qflags & QGEN_QVV)) )
        throw RuntimeError("Set the c-matrix and occupations first!");


    if(bsorder_ != BSORDER_PSI4)
    {
        // reorder the Cmat
        auto ord = reorder::GetOrdering(bsorder_);
    
        // only need to reorder the rows
        //std::cout << "BEFORE REORDERING:\n";
        //for(int i = 0; i < nmo_*nso_; i++)
        //    std::cout << Cmo_[i] << "\n";
        ReorderMatRows(Cmo_, ord, nmo_);
        //std::cout << "AFTER REORDERING:\n";
        //for(int i = 0; i < nmo_*nso_; i++)
        //    std::cout << Cmo_[i] << "\n";
    }

    // now we can split the c matrix
    // Whether we need the cmatrix or not has been checked above ^_^
    if(Cmo_)
        SplitCMat();

    if(qflags & QGEN_QSO)
    {
        qso_ = StoredQTensorFactory(storeflags | QSTORAGE_PACKED, "qso", directory_);
        if(qmo_->filled())
            qflags &= ~QGEN_QMO; // remove from list
    }
    if(qflags & QGEN_QMO)
    {
        qmo_ = StoredQTensorFactory(storeflags | QSTORAGE_PACKED, "qmo", directory_);
        if(qmo_->filled())
            qflags &= ~QGEN_QMO; // remove from list
    }
    if(qflags & QGEN_QOO)
    {
        qoo_ = StoredQTensorFactory(storeflags | QSTORAGE_PACKED, "qoo", directory_);
        if(qoo_->filled())
            qflags &= ~QGEN_QOO; // remove from list
    }
    if(qflags & QGEN_QOV)
    {
        qov_ = StoredQTensorFactory(storeflags, "qov", directory_);
        if(qov_->filled())
            qflags &= ~QGEN_QOV; // remove from list
    }
    if(qflags & QGEN_QVV)
    {
        // generate Qvv
        qvv_ = StoredQTensorFactory(storeflags | QSTORAGE_PACKED, "qvv", directory_);
        if(qvv_->filled())
            qflags &= ~QGEN_QVV; // remove from list
    }

    // call derived class GenQTensors_
    GenQTensors_(qflags, storeflags); 

    // if Qso is requested, we have to reorder it!
    if(qflags & QGEN_QSO && bsorder_ != BSORDER_PSI4)
    {
        ReorderQso();
    }


#ifdef PANACHE_TIMING
    tim.Stop();
    timer_genqtensors_.AddTime(tim);
#endif

}


void ThreeIndexTensor::ReorderQso(void)
{
    auto ord = reorder::GetOrdering(bsorder_);

    // First, generate an identity matrix
    std::unique_ptr<double[]> tmat(new double[nso2_]);
    double * tmatp = tmat.get();

    std::fill(tmatp, tmatp + nso2_, 0.0);
    for(int i = 0; i < nso2_; i += (nso_+1))
        tmat[i] = 1.0;

    // Make into a transformation matrix
    ReorderMatRows(tmat, ord, nso_);

    // transform
    qso_->Transform(nso_, tmatp, nso_, tmatp, nthreads_);
}


int ThreeIndexTensor::GetQBatch_Base(double * outbuf, int bufsize, int qstart,
                                    const UniqueStoredQTensor & qt)
{
#ifdef PANACHE_TIMING
    Timer tim;
    tim.Start();
#endif

    int nq = (bufsize / qt->ndim12());

    if(nq == 0)
        throw RuntimeError("Error - buffer is to small to hold even one batch!");

    // get a batch
    int gotten = qt->ReadByQ(outbuf, nq, qstart);

#ifdef PANACHE_TIMING
    tim.Stop();
    qt->GetQBatchTimer().AddTime(tim);
#endif

    return gotten;
}


int ThreeIndexTensor::GetBatch_Base(double * outbuf, int bufsize, int ijstart,
                                    const UniqueStoredQTensor & qt)
{
#ifdef PANACHE_TIMING
    Timer tim;
    tim.Start();
#endif

    int nij = (bufsize / qt->naux());

    if(nij == 0)
        throw RuntimeError("Error - buffer is to small to hold even one batch!");

    // get a batch
    int gotten = qt->Read(outbuf, nij, ijstart);

#ifdef PANACHE_TIMING
    tim.Stop();
    qt->GetBatchTimer().AddTime(tim);
#endif

    return gotten;
}


UniqueStoredQTensor & ThreeIndexTensor::ResolveTensorFlag(int tensorflag)
{
    switch(tensorflag)
    {
        case QGEN_QSO:
            return qso_;
        case QGEN_QMO:
            return qmo_;
        case QGEN_QOO:
            return qoo_;
        case QGEN_QOV:
            return qov_;
        case QGEN_QVV:
            return qvv_;
        default:
            throw RuntimeError("Unknown tensorflag");
    }
}


int ThreeIndexTensor::GetQBatch(int tensorflag, double * outbuf, int bufsize, int qstart)
{
    return GetQBatch_Base(outbuf, bufsize, qstart, ResolveTensorFlag(tensorflag));
}

int ThreeIndexTensor::GetBatch(int tensorflag, double * outbuf, int bufsize, int ijstart)
{
    return GetBatch_Base(outbuf, bufsize, ijstart, ResolveTensorFlag(tensorflag));
}


int ThreeIndexTensor::GetQBatch(int tensorflag, double * outbuf, int bufsize, QIterator qstart)
{
    if(qstart)
        return GetQBatch_Base(outbuf, bufsize, qstart.Index(), ResolveTensorFlag(tensorflag));
    else
        return 0;
}

int ThreeIndexTensor::GetBatch(int tensorflag, double * outbuf, int bufsize, IJIterator ijstart)
{
    if(ijstart)
        return GetBatch_Base(outbuf, bufsize, ijstart.Index(), ResolveTensorFlag(tensorflag));
    else
        return 0;
}


int ThreeIndexTensor::QBatchSize(int tensorflag)
{
    return ResolveTensorFlag(tensorflag)->ndim12();
}


int ThreeIndexTensor::BatchSize(int tensorflag)
{
    return ResolveTensorFlag(tensorflag)->naux();
}

int ThreeIndexTensor::TensorDimensions(int tensorflag, int & naux, int & ndim1, int & ndim2)
{
    auto & qt = ResolveTensorFlag(tensorflag);
    naux = qt->naux();
    ndim1 = qt->ndim1();
    ndim2 = qt->ndim2();
    return qt->naux() * qt->ndim12();
}


bool ThreeIndexTensor::IsPacked(int tensorflag)
{
    return ResolveTensorFlag(tensorflag)->packed();
}

int ThreeIndexTensor::CalcIndex(int tensorflag, int i, int j)
{
    return ResolveTensorFlag(tensorflag)->calcindex(i, j);
}

// note - passing by value for the vector
static void Reorder(std::vector<unsigned short> order, std::vector<double *> pointers,
                    reorder::MemorySwapper & sf)
{
    long int size = order.size();

    // original order is 1 2 3 4 5 6....
    std::vector<unsigned short> currentorder(size);

    for(long int i = 0; i < size; i++)
        currentorder[i] = i+1;

    for(long int i = 0; i < size; i++)
    {
        // find the index in the current order
        long int cindex = 0;
        bool found = false;

        for(int j = 0; j < size; j++)
        {
            if(currentorder[j] == order[i])
            {
                found = true;
                cindex = j;
                break;
            }
        }
        if(!found)
            throw RuntimeError("Error in reordering - index not found?");


        // we shouldn't swap anything that was previously put in place...
        if(cindex < i)
            throw RuntimeError("Error in reordering - going to swap something I shouldn't");

        //swap
        if(cindex != i)
        {
            sf.swap(pointers[i], pointers[cindex]);
            std::swap(currentorder[i], currentorder[cindex]);
        }
    }

    // double check
    for(long int i = 0; i < size; i++)
    {
        if(currentorder[i] != order[i])
            throw RuntimeError("Reordering failed!");
    }
}

void ThreeIndexTensor::RenormCMat(const std::unique_ptr<reorder::CNorm> & cnorm)
{
    using namespace reorder;

    for(int i = 0; i < primary_->nshell(); i++)
    {
        const GaussianShell & s = primary_->shell(i);
        int jstart = s.function_index();

        if(cnorm->NeedsCNorm(s.is_pure(), s.am()))
        {
            auto normfac = cnorm->GetCNorm(s.is_pure(), s.am());
            
            // multiply rows by factors
            for(size_t j = 0, js = jstart; j < normfac.size(); j++, js++)
                for(int k = 0; k < nmo_; k++)
                    Cmo_[js*nmo_+k] *= normfac[j];
        }
    }
}

void ThreeIndexTensor::ReorderMatRows(const std::unique_ptr<double[]> & mat,
                                      const std::unique_ptr<reorder::Orderings> & order,
                                      int ncol)
{
    using namespace reorder;

    TotalMemorySwapper sf1(ncol);  // swaps rows

    std::vector<PointerMap> vpm;

    //go through what would need to be changed in the primary basis
    for(int i = 0; i < primary_->nshell(); i++)
    {
        const GaussianShell & s = primary_->shell(i);
        if(order->NeedsInvReordering(s.is_pure(), s.am()))
            vpm.push_back(PointerMap(s.function_index(), order->GetInvOrder(s.is_pure(), s.am())));
    }

    std::vector<double *> pointers(primary_->max_function_per_shell());

    // Swap rows
    for(auto & it : vpm)
    {
        size_t ntoswap = it.order.size();

        for(size_t n = 0; n < ntoswap; n++)
            pointers[n] = &(mat[(it.start+n)*ncol]);

        Reorder(it.order, pointers, sf1);
    }
}

void ThreeIndexTensor::ReorderMatCols(const std::unique_ptr<double[]> & mat,
                                      const std::unique_ptr<reorder::Orderings> & order,
                                      int nrow)
{
    using namespace reorder;

    TotalMemorySwapper sf1(1);  // swaps elements

    std::vector<PointerMap> vpm;

    //go through what would need to be changed in the primary basis
    for(int i = 0; i < primary_->nshell(); i++)
    {
        const GaussianShell & s = primary_->shell(i);
        if(order->NeedsInvReordering(s.is_pure(), s.am()))
            vpm.push_back(PointerMap(s.function_index(), order->GetInvOrder(s.is_pure(), s.am())));
    }

    std::vector<double *> pointers(primary_->max_function_per_shell());

    // Swap columns
    for(int i = 0; i < nrow; i++)
    {
        int irow = i * nrow;

        for(auto & it : vpm)
        {
            size_t ntoswap = it.order.size();

            for(size_t n = 0; n < ntoswap; n++)
               pointers[n] = &(mat[irow + (it.start+n)]);

            Reorder(it.order, pointers, sf1);
        }
    }
}

void ThreeIndexTensor::SplitCMat(void)
{
    Cmo_occ_ = std::unique_ptr<double[]>(new double[nso_*nocc_]);
    Cmo_vir_ = std::unique_ptr<double[]>(new double[nso_*nvir_]);

    //std::fill(Cmo_occ_.get(), Cmo_occ_.get() + nso_*nocc_, 0.0);
    //std::fill(Cmo_vir_.get(), Cmo_vir_.get() + nso_*nvir_, 0.0);

    // note - Cmo_occ_ and Cmo_vir_ will always be in column major order!
    // Cmo_ is nso * nmo
    //! \todo BLAS call?
    for(int i = 0; i < nso_; i++)
    {
        // remember to remove the frozen orbitals
        for(int j = 0; j < nocc_; j++)
            Cmo_occ_[i*nocc_ + j] = Cmo_[i*nmo_+(j+nfroz_)];
        for(int j = 0; j < nvir_; j++)
            Cmo_vir_[i*nvir_ + j] = Cmo_[i*nmo_+(j+nocc_+nfroz_)];
    }
}

void ThreeIndexTensor::SetNOcc(int nocc, int nfroz)
{
    if(nocc <= 0)
        throw RuntimeError("Error - nocc <= 0!");

    if(Cmo_ == nullptr)
        throw RuntimeError("Error - C Matrix not set!");

    nocc_ = nocc;
    nfroz_ = nfroz;
    nvir_ = nmo_ - nocc - nfroz;

}


void ThreeIndexTensor::PrintTimer(const char * name, const UniqueStoredQTensor & q) const
{
    if(q)
    {
        // need to convert from std::atomic
        unsigned long genm = q->GenTimer().microseconds;
        unsigned long gent = q->GenTimer().timescalled;
        unsigned long getm = q->GetBatchTimer().microseconds;
        unsigned long gett = q->GetBatchTimer().timescalled;
        unsigned long getqm = q->GetQBatchTimer().microseconds;
        unsigned long getqt = q->GetQBatchTimer().timescalled;

        output::printf("%-6s  %17lu (%7lu)  %17lu (%7lu)  %17lu (%7lu)\n", name, 
                        genm, gent,
                        getm, gett,
                        getqm, getqt);
    }
    else
        output::printf("%-6s  %17s (%7s)  %17s (%7s)  %17s (%7s)\n", name, 
                              "N/A", "N/A", "N/A", "N/A", "N/A", "N/A"); 
}

void ThreeIndexTensor::PrintTimings(void) const
{
    #ifdef PANACHE_TIMING

    const char * name = "??";
    switch (qtype_)
    {
        case QTYPE_DFQSO:
        {
            name = "DF";
            break;
        }
        case QTYPE_CHQSO:
        {
            name = "Cholesky";
            break;
        }
        default:
            break;
    }

    output::printf("\n\n  ==> LibPANACHE %s Tensor Timings <==\n\n", name);
    output::printf("*Time (in microseconds), followed by number of times called in parentheses*\n");
    //output::printf(std::string(80, '-').c_str());
    output::printf("\n");
    output::printf("%-6s  %-27s  %-27s  %-27s\n",
                   "Tensor",
                   "        Generation", 
                   "         GetBatch", 
                   "         GetQBatch");
    output::printf("%-6s  %-27s  %-27s  %-27s\n", "------",
                   std::string(27,'-').c_str(), 
                   std::string(27,'-').c_str(), 
                   std::string(27,'-').c_str()); 
    PrintTimer("QSO", qso_);
    PrintTimer("QMO", qmo_);
    PrintTimer("QOO", qoo_);
    PrintTimer("QOV", qov_);
    PrintTimer("QVV", qvv_);
    output::printf(std::string(93, '-').c_str());
    output::printf("\n\n");

    #endif
}


ThreeIndexTensor::IteratedQTensorByQ ThreeIndexTensor::IterateByQ(int tensorflag, double * buf, int bufsize)
{
    using std::placeholders::_1;

    // ugly
    IteratedQTensorByQ::GetBatchFunc gbf(std::bind(
                                   static_cast<int(ThreeIndexTensor::*)(int, double *, int, int)>(&ThreeIndexTensor::GetQBatch),
                                   this, tensorflag, buf, bufsize, _1));

    int ndim1, ndim2, naux;
    TensorDimensions(tensorflag, naux, ndim1, ndim2);

    IteratedQTensorByQ iqt(tensorflag, buf, bufsize, 
                           QBatchSize(tensorflag),
                           QIterator(naux, IsPacked(tensorflag)),
                           gbf);

    return iqt;
}


ThreeIndexTensor::IteratedQTensorByIJ ThreeIndexTensor::IterateByIJ(int tensorflag, double * buf, int bufsize)
{
    using std::placeholders::_1;

    // ugly
    IteratedQTensorByIJ::GetBatchFunc gbf(std::bind(
                                   static_cast<int(ThreeIndexTensor::*)(int, double *, int, int)>(&ThreeIndexTensor::GetBatch),
                                   this, tensorflag, buf, bufsize, _1));

    int ndim1, ndim2, naux;
    TensorDimensions(tensorflag, naux, ndim1, ndim2);

    IteratedQTensorByIJ iqt(tensorflag, buf, bufsize, 
                            BatchSize(tensorflag),
                            IJIterator(ndim1, ndim2, IsPacked(tensorflag)),
                            gbf);

    return iqt;
}


void ThreeIndexTensor::Transform_(int nso, int nso2, int np, double * source,
                                  int nleft, double * left,
                                  int nright, double * right,
                                  double * dest, double * work)
{
    for(int i = 0; i < np; i++)
    {
        C_DGEMM('T', 'N', nleft, nso, nso, 
                         1.0, left, nleft,
                         source + i*nso2, nso,
                         0.0, work + i*nleft*nso, nso);
    }
    for(int i = 0; i < np; i++)
    {

        C_DGEMM('N', 'N', nleft, nright, nso, 
                         1.0, work + i*nleft*nso, nso,
                         right, nright,
                         0.0, dest + i*nleft*nright, nright);
    }
}

} // close namespace panache


