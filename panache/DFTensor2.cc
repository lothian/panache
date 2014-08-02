/*! \file
 * \brief Density fitting tensor generation and manipulation (source)
 * \author Benjamin Pritchard (ben@bennyp.org)
 */


#include <fstream>
#include <algorithm>
#include <iostream>
#include "panache/DFTensor2.h"
#include "panache/FittingMetric.h"
#include "panache/Molecule.h"
#include "panache/BasisSet.h"
#include "panache/Lapack.h"
#include "panache/Exception.h"

// for reordering
#include "panache/Orderings.h"
#include "panache/MemorySwapper.h"

#include "panache/ERI.h"

#include "panache/ERDERI.h"
#include "panache/Output.h"

#ifdef _OPENMP
#include <omp.h>
#endif

namespace panache
{

DFTensor2::DFTensor2(SharedBasisSet primary,
                     SharedBasisSet auxiliary,
                     const std::string & directory,
                     int nthreads)
    : primary_(primary), auxiliary_(auxiliary), nthreads_(nthreads)
{
    output::printf("  ==> LibPANACHE DF Tensor <==\n\n");

    output::printf(" => Primary Basis Set <= \n\n");
    primary_->print_detail();

    output::printf(" => Auxiliary Basis Set <= \n\n");
    auxiliary_->print_detail();

#ifdef _OPENMP
    if(nthreads_ <= 0)
        nthreads_ = omp_get_max_threads();
#else
    nthreads_ = 1;
#endif

    fittingmetric_ = std::shared_ptr<FittingMetric>(new FittingMetric(auxiliary_, nthreads_));
    fittingmetric_->form_eig_inverse();


    Cmo_ = nullptr;
    nmo_ = 0;
    nmo2_ = 0;
    nso_ = primary_->nbf();
    nso2_ = nso_*nso_;
    naux_ = auxiliary_->nbf();
}


int DFTensor2::SetNThread(int nthread)
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



DFTensor2::~DFTensor2()
{
}

void DFTensor2::GenQso(QStorage storetype)
{
    if(qso_)
        return; // already created!


#ifdef PANACHE_TIMING
    timer_genqso.Reset();
    timer_genqso.Start();
#endif

    qso_ = StoredQTensorFactory(naux_, nso_, nso_, true, true, storetype);
    qso_->Init();

    int maxpershell = primary_->max_function_per_shell();
    int maxpershell2 = maxpershell*maxpershell;

    double * J = fittingmetric_->get_metric();

    // default constructor = zero basis
    SharedBasisSet zero(new BasisSet);

    std::vector<std::shared_ptr<TwoBodyAOInt>> eris;
    std::vector<const double *> eribuffers;
    std::vector<double *> A, B;

    for(int i = 0; i < nthreads_; i++)
    {
        eris.push_back(GetERI(auxiliary_,zero,primary_,primary_));
        eribuffers.push_back(eris[eris.size()-1]->buffer());

        // temporary buffers
        A.push_back(new double[naux_*maxpershell2]);
        B.push_back(new double[naux_*maxpershell2]);
    }


    #ifdef _OPENMP
        #pragma omp parallel for schedule(dynamic) num_threads(nthreads_)
    #endif
    for (int M = 0; M < primary_->nshell(); M++)
    {
        int threadnum = 0;

        #ifdef _OPENMP
        threadnum = omp_get_thread_num();
        #endif

        int nm = primary_->shell(M).nfunction();
        int mstart = primary_->shell(M).function_index();
        int mend = mstart + nm;

        for (int N = 0; N < M; N++)
        {
            int nn = primary_->shell(N).nfunction();
            int nstart = primary_->shell(N).function_index();
            int nend = nstart + nn;

            for (int P = 0; P < auxiliary_->nshell(); P++)
            {
                int np = auxiliary_->shell(P).nfunction();
                int pstart = auxiliary_->shell(P).function_index();
                int pend = pstart + np;

                int ncalc = eris[threadnum]->compute_shell(P,0,M,N);

                if(ncalc)
                {
                    for (int p = pstart, index = 0; p < pend; p++)
                    {
                        for (int m = 0; m < nm; m++)
                        {
                            for (int n = 0; n < nn; n++, index++)
                            {
                                B[threadnum][p*nm*nn + m*nn + n] = eribuffers[threadnum][index];
                            }
                        }
                    }
                }
            }

            // we now have a set of columns of B, although "condensed"
            // we can do a DGEMM with J
            // Access to J are only reads, so that is safe in parallel
            C_DGEMM('T','T',nm*nn, naux_, naux_, 1.0, B[threadnum], nm*nn, J, naux_, 0.0,
                    A[threadnum], naux_);


            // write to disk or store in memory
           for (int m0 = 0, m = mstart; m < mend; m0++, m++)
               for (int n0 = 0, n = nstart; n < nend; n0++, n++)
                   qso_->Write(A[threadnum] + (m0*nn+n0)*naux_, m, n);
        }

        // Special Case: N = M
        for (int P = 0; P < auxiliary_->nshell(); P++)
        {
            int np = auxiliary_->shell(P).nfunction();
            int pstart = auxiliary_->shell(P).function_index();
            int pend = pstart + np;

            eris[threadnum]->compute_shell(P,0,M,M);

            for (int p = pstart, index = 0; p < pend; p++)
            {
                for (int m = 0; m < nm; m++)
                {
                    for (int n = 0; n < nm; n++, index++)
                    {
                        B[threadnum][p*nm*nm + m*nm + n] = eribuffers[threadnum][index];
                    }
                }
            }
        }

        // we now have a set of columns of B, although "condensed"
        // we can do a DGEMM with J
        C_DGEMM('T','T',nm*nm, naux_, naux_, 1.0, B[threadnum], nm*nm, J, naux_, 0.0,
                A[threadnum], naux_);


        // write to disk or store in memory
        for (int m0 = 0, m = mstart; m < mend; m0++, m++)
            for (int n0 = 0, n = mstart; n < mend; n0++, n++)
                if(m <= n)
                    qso_->Write(A[threadnum] + (m0*nm+n0)*naux_, m, n);
    }

    for(int i = 0; i < nthreads_; i++)
    {
        delete [] A[i];
        delete [] B[i];
    }

}

int DFTensor2::QsoDimensions(int & naux, int & nso2)
{
    nso2 = nso2_;
    naux = naux_;
    return nso2*naux;
}

void DFTensor2::SetCMatrix(double * cmo, int nmo, bool cmo_is_trans,
                           BSOrder order)
{
    nmo_ = nmo;
    nmo2_ = nmo*nmo;

    Cmo_ = std::unique_ptr<double[]>(new double[nmo_*nso_]);

    if(cmo_is_trans)
    {
        for(int i = 0; i < nso_; i++)
            for(int j = 0; j < nmo_; j++)
                Cmo_[i*nmo_+j] = cmo[j*nso_+i];
    }
    else
        std::copy(cmo, cmo+(nmo_*nso_), Cmo_.get());

    if(order != BSOrder::Psi4)
    {
        reorder::Orderings * ord;

        if (order == BSOrder::GAMESS)
            ord = new reorder::GAMESS_Ordering();
        else
            throw RuntimeError("Unknown ordering!");

        //std::cout << "BEFORE REORDERING:\n";
        //for(int i = 0; i < nmo_*nso_; i++)
        //    std::cout << Cmo_[i] << "\n";
        ReorderCMat(*ord);
        //std::cout << "AFTER REORDERING:\n";
        //for(int i = 0; i < nmo_*nso_; i++)
        //    std::cout << Cmo_[i] << "\n";
        delete ord;
    }
}


void DFTensor2::TransformQTensor(double * left, int lncols,
                                 double * right, int rncols,
                                 std::unique_ptr<StoredQTensor> & qout,
                                 int q,
                                 double * qso, double * qc, double * cqc)
{
    // transform
    C_DGEMM('T', 'N', lncols, nso_, nso_, 1.0, left, lncols, qso, nso_, 0.0, qc, nso_);
    C_DGEMM('N', 'N', lncols, rncols, nso_, 1.0, qc, nso_, right, rncols, 0.0, cqc, rncols);

    // Write to memory or disk
    qout->WriteByQ(cqc, 1, q);
}


// Bit    Matrix
// 1      Qmo
// 2      Qoo
// 3      Qov
// 4      Qvv
void DFTensor2::GenQTensors(int qflags, QStorage storetype)
{
    // temporary space
    // note that nmo, occupied, etc, are all <= nso
    // so this is the max space that would be needed
    double * buf = new double[nso2_*nthreads_];
    double * qc = new double[nso2_*nthreads_];
    double * cqc = new double[nso2_*nthreads_];

    GenQso(storetype);

    if((qflags & (15)) == 0)
        return; //?

    if(!Cmo_)
        throw RuntimeError("Set the c-matrix first!");

    if(qflags & (1 << 0))
    {
        // generate Qmo
        qmo_ = StoredQTensorFactory(naux_, nmo_, nmo_, false, false, storetype);
        qmo_->Init();
    }
    if(qflags & (1 << 1))
    {
        // generate Qoo
        qoo_ = StoredQTensorFactory(naux_, nocc_, nocc_, false, false, storetype);
        qoo_->Init();
    }
    if(qflags & (1 << 2))
    {
        // generate Qov
        qov_ = StoredQTensorFactory(naux_, nocc_, nvir_, false, false, storetype);
        qov_->Init();
    }
    if(qflags & (1 << 3))
    {
        // generate Qvv
        qvv_ = StoredQTensorFactory(naux_, nvir_, nvir_, false, false, storetype);
        qvv_->Init();
    }

  
    // Read in Qso, and create any requested tensors 
    #ifdef _OPENMP
        #pragma omp parallel for num_threads(nthreads_)
    #endif
    for(int q = 0; q < naux_; q++)
    {
        int threadnum = 0;

        #ifdef _OPENMP
        threadnum = omp_get_thread_num();
        #endif

        double * myqso = buf + threadnum*nso2_;
        double * myqc = qc + threadnum*nso2_;
        double * mycqc = cqc + threadnum*nso2_;

        qso_->ReadByQ(myqso, 1, q);


        if(qflags & (1 << 0))
            TransformQTensor(Cmo_.get(), nmo_, Cmo_.get(), nmo_, qmo_, q, myqso, myqc, mycqc);
        if(qflags & (1 << 1))
            TransformQTensor(Cmo_occ_.get(), nocc_, Cmo_occ_.get(), nocc_, qoo_, q, myqso, myqc, mycqc);
        if(qflags & (1 << 2))
            TransformQTensor(Cmo_occ_.get(), nocc_, Cmo_vir_.get(), nvir_, qov_, q, myqso, myqc, mycqc);
        if(qflags & (1 << 3))
            TransformQTensor(Cmo_vir_.get(), nvir_, Cmo_vir_.get(), nvir_, qvv_, q, myqso, myqc, mycqc);
    }


    delete [] buf;
    delete [] qc;
    delete [] cqc;
}



int DFTensor2::GetBatch_Qso(double * outbuf, int bufsize, int qstart)
{
    if(bufsize < nso2_)
        throw RuntimeError("Error - buffer is to small to hold even one row!");

    int nq = (bufsize / nso2_);

    // get a batch
    int gotten = qso_->ReadByQ(outbuf, nq, qstart);

    return gotten;
}

int DFTensor2::GetBatch_Qmo(double * outbuf, int bufsize, int qstart)
{
    if(bufsize < nmo2_)
        throw RuntimeError("Error - buffer is to small to hold even one row!");

    int nq = (bufsize / nmo2_);

    // get a batch
    int gotten = qmo_->ReadByQ(outbuf, nq, qstart);

    return gotten;
}

int DFTensor2::GetBatch_Qoo(double * outbuf, int bufsize, int qstart)
{
    if(bufsize < nocc_*nocc_)
        throw RuntimeError("Error - buffer is to small to hold even one row!");

    int nq = (bufsize / (nocc_*nocc_));

    // get a batch
    int gotten = qoo_->ReadByQ(outbuf, nq, qstart);

    return gotten;
}

int DFTensor2::GetBatch_Qov(double * outbuf, int bufsize, int qstart)
{
    if(bufsize < nocc_*nvir_)
        throw RuntimeError("Error - buffer is to small to hold even one row!");

    int nq = (bufsize / (nocc_*nvir_));

    // get a batch
    int gotten = qov_->ReadByQ(outbuf, nq, qstart);

    return gotten;
}

int DFTensor2::GetBatch_Qvv(double * outbuf, int bufsize, int qstart)
{
    if(bufsize < nvir_*nvir_)
        throw RuntimeError("Error - buffer is to small to hold even one row!");

    int nq = (bufsize / (nvir_*nvir_));

    // get a batch
    int gotten = qvv_->ReadByQ(outbuf, nq, qstart);

    return gotten;
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

void DFTensor2::ReorderCMat(reorder::Orderings & order)
{
    using namespace reorder;
    using std::placeholders::_1;
    using std::placeholders::_2;

    TotalMemorySwapper sf1(nmo_);  // swaps rows

    std::vector<PointerMap> vpm;

    //go through what would need to be changed in the primary basis
    for(int i = 0; i < primary_->nshell(); i++)
    {
        const GaussianShell & s = primary_->shell(i);
        if(s.is_pure())
        {
            if(order.NeedsInvSphReordering(s.am()))
                vpm.push_back(PointerMap(s.function_index(), order.GetInvSphOrder(s.am())));
        }
        else
        {
            if(order.NeedsInvCartReordering(s.am()))
                vpm.push_back(PointerMap(s.function_index(), order.GetInvCartOrder(s.am())));
        }
    }


    std::vector<double *> pointers(primary_->max_function_per_shell());


    // Swap rows
    for(auto & it : vpm)
    {
        size_t ntoswap = it.order.size();

        for(size_t n = 0; n < ntoswap; n++)
            pointers[n] = &(Cmo_[(it.start+n)*nmo_]);

        Reorder(it.order, pointers, sf1);
    }
}

void DFTensor2::SplitCMat(void)
{
    Cmo_occ_ = std::unique_ptr<double[]>(new double[nso_*nocc_]);
    Cmo_vir_ = std::unique_ptr<double[]>(new double[nso_*nvir_]);

    std::fill(Cmo_occ_.get(), Cmo_occ_.get() + nso_*nocc_, 0.0);
    std::fill(Cmo_vir_.get(), Cmo_vir_.get() + nso_*nvir_, 0.0);

    // note - Cmo_occ_ and Cmo_vir_ will always be in column major order!
    // Cmo_ is nso * nmo
    //! \todo BLAS call?
    for(int i = 0; i < nso_; i++)
    {
        for(int j = 0; j < nocc_; j++)
            Cmo_occ_[i*nocc_ + j] = Cmo_[i*nmo_+j];
        for(int j = 0; j < nvir_; j++)
            Cmo_vir_[i*nvir_ + j] = Cmo_[i*nmo_+(j+nocc_)];
    }
}

void DFTensor2::SetNOcc(int nocc, int nfroz)
{
    if(nocc <= 0)
        throw RuntimeError("Error - nocc <= 0!");

    if(Cmo_ == nullptr)
        throw RuntimeError("Error - C Matrix not set!");

    nocc_ = nocc;
    nfroz_ = nfroz;
    nvir_ = nmo_ - nocc;

    SplitCMat();
}

} // close namespace panache



