/*! \file
 * \brief Generic three-index tensor storage (source)
 * \author Benjamin Pritchard (ben@bennyp.org)
 */

#include "panache/storedqtensor/StoredQTensor.h"
#include "panache/Exception.h"
#include "panache/Flags.h"

namespace panache
{


int StoredQTensor::naux(void) const
{
    return naux_;
}

int StoredQTensor::ndim1(void) const
{
    return ndim1_;
}

int StoredQTensor::ndim2(void) const
{
    return ndim2_;
}

int StoredQTensor::ndim12(void) const
{
    return ndim12_;
}

int StoredQTensor::storesize(void) const
{
    return ndim12_*naux_;
}

int StoredQTensor::packed(void) const
{
    return (storeflags_ & QSTORAGE_PACKED);
}

int StoredQTensor::byq(void) const
{
    return (storeflags_ & QSTORAGE_BYQ);
}

const std::string & StoredQTensor::name(void) const
{
    return name_;
}

int StoredQTensor::calcindex(int i, int j) const
{
    if(!packed())
        return (i*ndim2_+j);
    else if(i >= j)
        return ((i*(i+1))>>1) + j;
    else
        return ((j*(j+1))>>1) + i;
}

void StoredQTensor::Init(int naux, int ndim1, int ndim2, int storeflags, const std::string & name)
{
    naux_ = naux;
    ndim1_ = ndim1;
    ndim2_ = ndim2;
    storeflags_ = storeflags;

    if(packed() && ndim1 != ndim2)
        throw RuntimeError("non square packed matrices?");

    ndim12_ = (packed() ? (ndim1_ * (ndim2_+1))/2 : ndim1_*ndim2_);
    name_ = name;

    Init_();
}

StoredQTensor::~StoredQTensor()
{
}

int StoredQTensor::StoreFlags(void) const
{
    return storeflags_;
}

int StoredQTensor::Read(double * data, int nij, int ijstart)
{
    // note - don't check (nij > 0)!
    // it may occassionally be called with nij = 0

    if(nij < 0)
        throw RuntimeError("Read() passed with negative nij!");

    if(ijstart + nij >= ndim12())
        nij = ndim12() - ijstart;

    Read_(data, nij, ijstart);
    return nij;
}

int StoredQTensor::ReadByQ(double * data, int nq, int qstart)
{
    // note - don't check (nq > 0)!
    // it may occassionally be called with nq = 0

    if(nq < 0)
        throw RuntimeError("Read() passed with negative nq!");

    if(qstart + nq >= naux_)
        nq = naux_-qstart;

    ReadByQ_(data, nq, qstart);
    return nq;
}

void StoredQTensor::Clear(void)
{
    Clear_();
}

StoredQTensor::StoredQTensor(void)
{
}


CumulativeTime & StoredQTensor::GenTimer(void)
{
    return gen_timer_;
}

CumulativeTime & StoredQTensor::GetQBatchTimer(void)
{
    return getqbatch_timer_;
}

CumulativeTime & StoredQTensor::GetBatchTimer(void)
{
    return getijbatch_timer_;
}

void StoredQTensor::GenDFQso(const SharedFittingMetric & fit,
                                     const SharedBasisSet primary,
                                     const SharedBasisSet auxiliary,
                                     int nthreads)
{
#ifdef PANACHE_TIMING
    Timer tim;
    tim.Start();
#endif

    GenDFQso_(fit, primary, auxiliary, nthreads);

#ifdef PANACHE_TIMING
    tim.Stop();
    GenTimer().AddTime(tim);
#endif
}

void StoredQTensor::GenCHQso(const SharedBasisSet primary,
                                     double delta,
                                     int storeflags,
                                     int nthreads)
{
#ifdef PANACHE_TIMING
    Timer tim;
    tim.Start();
#endif

    GenCHQso_(primary, delta, storeflags, nthreads);

#ifdef PANACHE_TIMING
    tim.Stop();
    GenTimer().AddTime(tim);
#endif
}


void StoredQTensor::Transform(const std::vector<TransformMat> & left,
                                        const std::vector<TransformMat> & right,
                                        std::vector<StoredQTensor *> results,
                                        int nthreads)
{
    Transform_(left, right, results, nthreads);
}

int StoredQTensor::ContractSingle(StoredQTensor * rhs, int i, int j, int k, int l, double * out,
                                  std::vector<double> & scratch)
{
    auto res = ContractMulti(rhs, calcindex(i, j), calcindex(k, l), 1, 1, out, scratch);
    return res.first * res.second; // 1 or 0
}


std::pair<int, int>
StoredQTensor::ContractMulti(StoredQTensor * rhs, int ij, int kl, int nij, int nkl,
                             double * out, std::vector<double> & scratch)
{
    if(scratch.size() < static_cast<size_t>(((nij+nkl)*naux())))
        throw RuntimeError("Scratch space not large enough for contraction!");
    return ContractMulti_(rhs, ij, kl, nij, nkl, out, scratch.data());
}


} // close namespace panache

