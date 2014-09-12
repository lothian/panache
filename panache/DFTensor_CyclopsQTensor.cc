#include <fstream>

#include "panache/Exception.h"
#include "panache/DFTensor.h"
#include "panache/Parallel.h"
#include "panache/ERI.h"
#include "panache/FittingMetric.h"
#include "panache/BasisSet.h"

namespace panache
{


//////////////////////////////
// CyclopsQTensor
//////////////////////////////
void DFTensor::CyclopsQTensor::Reset_(void)
{
    // nothing needed
}

void DFTensor::CyclopsQTensor::Read_(double * data, int nij, int ijstart)
{
    size_t nelements = nij*naux();

    std::vector<long> indices;
    indices.reserve(nelements);

    IJIterator it(ndim1(), ndim2(), packed());
    it += ijstart;

    if(byq())
    {
        for(int ij = 0; ij < nij; ij++)
        {
            for(int q = 0; q < naux(); q++)
                indices.push_back(q+it.i()*naux()+it.j()*naux()*ndim1());
            ++it;
        }
    }
    else
    {
        for(int ij = 0; ij < nij; ij++)
        {
            for(int q = 0; q < naux(); q++)
                indices.push_back(it.i()+it.j()*ndim1()+q*ndim1()*ndim2());
            ++it;
        }
    }

    tensor_->read(nelements, indices.data(), data);
}

void DFTensor::CyclopsQTensor::ReadByQ_(double * data, int nq, int qstart)
{
    size_t nelements = nq*ndim12();

    std::vector<long> indices;
    indices.reserve(nelements);

    if(byq())
    {
        for(int q = 0; q < naux(); q++)
        {
            IJIterator it(ndim1(), ndim2(), packed());

            for(int ij = 0; ij < ndim12(); ij++)
            {
                indices.push_back((q+qstart)+it.i()*naux()+it.j()*naux()*ndim1());
                ++it;
            }
        }
    }
    else
    {
        for(int q = 0; q < naux(); q++)
        {
            IJIterator it(ndim1(), ndim2(), packed());

            for(int ij = 0; ij < ndim12(); ij++)
            {
                indices.push_back(it.i()+it.j()*ndim1()+(q+qstart)*ndim1()*ndim2());
                ++it;
            }
        }
    }

    tensor_->read(nelements, indices.data(), data);
}

void DFTensor::CyclopsQTensor::Clear_(void)
{
    //?
    tensor_.release();
}

void DFTensor::CyclopsQTensor::Init_(void)
{
}


DFTensor::CyclopsQTensor::CyclopsQTensor(int naux, int ndim1, int ndim2, int storeflags, const std::string & name)
            : StoredQTensor(naux, ndim1, ndim2, storeflags), name_(name)
{
    int dimsIJ[3] = {ndim1, ndim2, naux};
    int dimsQ[3] = {naux, ndim1, ndim2};
    int * dims = dimsIJ;

    int symsNS[3] = {NS, NS, NS};
    int symIJ[3] = {SY, NS, NS};
    int symQ[3] = {NS, SY, NS};
    int * syms = symsNS;

    if(byq())
        dims = dimsQ;

    if(packed())
    {
        if(byq())
            syms = symQ;
        else
            syms = symIJ;
    }


    tensor_ = std::unique_ptr<CTF_Tensor>(new CTF_Tensor(3, dims, syms, parallel::CTFWorld(), name_.c_str()));

}


void DFTensor::CyclopsQTensor::DecomposeIndex_(int64_t index, int & i, int & j, int & q)
{
    if(byq())
    {
        j = index / (ndim1() * naux());
        index -= j * ndim1() * naux();
        i = index / naux();
        q = index - i * naux();
    }
    else
    {
        q = index / (ndim1() * ndim2());
        index -= q * ndim1() * ndim2();
        j = index / ndim1();
        i = index - j * ndim1();
    }
}

std::unique_ptr<CTF_Matrix>
DFTensor::CyclopsQTensor::FillWithMatrix_(double * mat, int nrow, int ncol, int sym, const char * name)
{
    std::unique_ptr<CTF_Matrix> ret(new CTF_Matrix(nrow, ncol, sym, parallel::CTFWorld(), name));

    int64_t np;
    int64_t * idx;
    double * data;

    ret->read_local(&np, &idx, &data);

    for(int64_t n = 0; n < np; n++)
    {
        // note that indicies are in column major order
        // so we have to convert them
        int64_t col = idx[n] / nrow;
        int64_t row = idx[n] - col * nrow;

        data[n] = mat[row * ncol + col];
    }

    ret->write(np, idx, data);

    free(idx);
    free(data);

    return ret;
}


void DFTensor::CyclopsQTensor::GenQso_(const std::shared_ptr<FittingMetric> & fit,
                                       const SharedBasisSet primary,
                                       const SharedBasisSet auxiliary,
                                       int nthreads)
{
    // nthreads is ignored

    // Distributed J matrix
    std::unique_ptr<CTF_Matrix> ctfj(FillWithMatrix_(fit->get_metric(), naux(), naux(), NS, "Jmat"));

    // Now fill up a base matrix
    SharedBasisSet zero(new BasisSet);
    std::shared_ptr<TwoBodyAOInt> eri = GetERI(auxiliary, zero, primary, primary);

    int64_t np;
    int64_t *idx;
    double * data;

    // make a tensor of the same size, but don't copy the data
    CTF_Tensor base(*tensor_, false);

    base.read_local(&np, &idx, &data);

    int i, j, q;

    //! \todo openmp here?
    for(int64_t n = 0; n < np; n++)
    {
        DecomposeIndex_(idx[n], i, j, q);
        data[n] = eri->compute_basisfunction(q, 0, i, j);
    }

    base.write(np, idx, data);

    free(idx);
    free(data);

    // contract!
    //! \todo check RVO
    if(byq())
        (*tensor_)["iab"] = (*ctfj)["ij"]*base["jab"];
    else
        (*tensor_)["abi"] = (*ctfj)["ij"]*base["abj"];

    std::cout << "QSO GENERATED\n";
}


void DFTensor::CyclopsQTensor::Transform_(const std::vector<TransformMat> & left,
                                          const std::vector<TransformMat> & right,
                                          std::vector<StoredQTensor *> results,
                                          int nthreads)
{
    // nthreads is ignored

    for(size_t i = 0; i < left.size(); i++)
    {
        CyclopsQTensor * rptr = dynamic_cast<CyclopsQTensor *>(results[i]);
        if(rptr == nullptr)
            throw RuntimeError("Cannot store result of Cyclops contraction in non-cyclops object");

        // create CTF Tensors for left and right
        auto ctfleft = FillWithMatrix_(left[i].first, ndim1(), left[i].second, NS, "LEFT");
        auto ctfright = FillWithMatrix_(right[i].first, ndim2(), right[i].second, NS, "RIGHT");

        // we need an intermediate tensor
        int syms[3] = { NS, NS, NS };
        int dimsQ[3] = { naux(), left[i].second, ndim1() };
        int dimsIJ[3] = { left[i].second, ndim1(), naux() };
        int * dims = dimsIJ;
        if(byq())
            dims = dimsQ;

        CTF_Tensor cq(3, dims, syms, parallel::CTFWorld(), "CQ");

        // contract into result tensor
        if(byq())
        {
            if(rptr->byq())
            {
                cq["qij"] = ((*ctfleft)["ai"] * (*tensor_)["qaj"]);
                (*(rptr->tensor_))["qij"] = cq["qia"] * (*ctfright)["aj"];
            }
            else
            {
                cq["qij"] = ((*ctfleft)["ai"] * (*tensor_)["qaj"]);
                (*(rptr->tensor_))["ijq"] = cq["qia"] * (*ctfright)["aj"];
            }
        }
        else
        {
            if(rptr->byq())
            {
                cq["ijq"] = ((*ctfleft)["ai"] * (*tensor_)["ajq"]);
                (*(rptr->tensor_))["qij"] = cq["iaq"] * (*ctfright)["aj"];
            }
            else
            {
                cq["ijq"] = ((*ctfleft)["ai"] * (*tensor_)["ajq"]);
                (*(rptr->tensor_))["ijq"] = cq["iaq"] * (*ctfright)["aj"];
            }
        }
    }
}


} // close namespace panache

