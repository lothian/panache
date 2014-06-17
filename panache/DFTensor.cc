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
#include <sstream>
#include <fstream>
#include <algorithm>
#include <utility>
#include <cctype>

#include "DFTensor.h"
#include "FittingMetric.h"
#include "Molecule.h"
#include "BasisSet.h"
#include "Lapack.h"

// for reordering
#include "Orderings.h"
#include "MemorySwapper.h"

#include "ERI.h"

#include "ERDERI.h"
#include "Output.h"

#ifndef PANACHE_QBUF_SIZE
#define PANACHE_QBUF_SIZE 10
#endif

#ifdef _OPENMP
#include <omp.h>
#endif

namespace panache
{

DFTensor::DFTensor(std::shared_ptr<BasisSet> primary,
                   std::shared_ptr<BasisSet> auxiliary,
                   const std::string & filename)
    : primary_(primary), auxiliary_(auxiliary), filename_(filename)
{
    common_init();
    curq_ = 0;
    Cmo_ = nullptr;
    Cmo_trans_ = false;
    nmo_ = 0;
    nmo2_ = 0;
    nso_ = primary_->nbf();
    nso2_ = nso_*nso_;
    nsotri_ = ( (nso_ * (nso_+1) ) )/2;
    naux_ = auxiliary_->nbf();

    outbuffer_ = nullptr;
    outbuffersize_ = 0; 



    nthreads_ = 1;
    #ifdef _OPENMP
      nthreads_ = omp_get_max_threads();
    #endif
}

DFTensor::~DFTensor()
{
    CloseFile();
}

void DFTensor::common_init()
{
    //! \todo remote print?
    print_ = 0;
    debug_ = 0;

    print_header();

    molecule_ = primary_->molecule();

    build_metric();
}

void DFTensor::print_header()
{
    output::printf("  ==> DF Tensor (by Rob Parrish) <==\n\n");

    output::printf(" => Primary Basis Set <= \n\n");
    primary_->print_detail();

    output::printf(" => Auxiliary Basis Set <= \n\n");
    auxiliary_->print_detail();
}

void DFTensor::build_metric()
{
    std::shared_ptr<FittingMetric> met(new FittingMetric(auxiliary_, true));
    met->form_eig_inverse();
    metric_ = met->get_metric();
}

int DFTensor::QsoDimensions(int & naux, int & nso2)
{
    nso2 = nso2_;
    naux = naux_;
    return nso2*naux;
}

void DFTensor::SetCMatrix(double * cmo, int nmo, bool cmo_is_trans)
{
    Cmo_ = cmo;
    Cmo_trans_ = cmo_is_trans;
    nmo_ = nmo;
    nmo2_ = nmo*nmo;
}

void DFTensor::GenQso(bool inmem)
{

#ifdef PANACHE_TIMING
    timer_genqso.Reset();
    timer_genqso.Start();
#endif

    int maxpershell = primary_->max_function_per_shell();
    int maxpershell2 = maxpershell*maxpershell;

    double** Jp = metric_->pointer();

    std::shared_ptr<BasisSet> zero = BasisSet::zero_ao_basis_set();

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

    isinmem_ = inmem; // store this so we know later

    qso_.reset();
    curq_ = 0;
    CloseFile(); // safe even if not opened

    // Allocate memory or open the file on disk
    if(!isinmem_)
    {
        OpenFile();
        ResetFile();
    }
    else
        qso_ = std::unique_ptr<double[]>(new double[nsotri_ * naux_]);


    

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

                eris[threadnum]->compute_shell(P,0,M,N);

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

            // we now have a set of columns of B, although "condensed"
            // we can do a DGEMM with J
            // Access to Jp are only reads, so that is safe in parallel
            C_DGEMM('N','N',naux_, nm*nn, naux_, 1.0, Jp[0], naux_, B[threadnum], nm*nn, 0.0,
                    A[threadnum], nm*nn);


            // write to disk or store in memory
            //! \todo rearrange to that writes are more sequential?
            if(isinmem_)
            {
                // safe to write to qso_ in parallel - each thread writes to a different spot
                for (int p = 0; p < naux_; p++)
                {
                    for (int m0 = 0, m = mstart; m < mend; m0++, m++)
                        std::copy(A[threadnum]+p*nm*nn+m0*nn,
                                  A[threadnum]+p*nm*nn+m0*nn+nn, 
                                  qso_.get() + p*nsotri_ + ((m*(m+1))>>1) + nstart);
                }
            }
            else
            {
                for (int p = 0; p < naux_; p++)
                {
                    for (int m0 = 0, m = mstart; m < mend; m0++, m++)
                    {
                        // must be done in a critical section
                        #ifdef _OPENMP
                        #pragma omp critical
                        #endif
                        {
                            matfile_->seekp(sizeof(double)*(p*nsotri_ + ((m*(m+1))>>1) + nstart), std::ios_base::beg);
                            matfile_->write(reinterpret_cast<const char *>(A[threadnum] + p*nm*nn + m0*nn), nn*sizeof(double));
                        }
                    }
                }
            }
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
        C_DGEMM('N','N',naux_, nm*nm, naux_, 1.0, Jp[0], naux_, B[threadnum], nm*nm, 0.0,
                A[threadnum], nm*nm);


        // write to disk
        //! \todo rearrange to that writes are more sequential?
        if(isinmem_)
        {
            // safe to write to qso_ in parallel - each thread writes to a different spot
            for (int p = 0; p < naux_; p++)
            {
                int nwrite = 1;
                for (int m0 = 0, m = mstart; m < mend; m0++, m++)
                {
                    std::copy(A[threadnum]+p*nm*nm+m0*nm,
                              A[threadnum]+p*nm*nm+m0*nm+nwrite,
                              qso_.get()+p*nsotri_ + ((m*(m+1))>>1) + mstart);
                    nwrite += 1;
                }
            }
        }
        else
        {
            for (int p = 0; p < naux_; p++)
            {
                int nwrite = 1;
                for (int m0 = 0, m = mstart; m < mend; m0++, m++)
                {
                    // must be done in a critical section
                    #ifdef _OPENMP
                    #pragma omp critical
                    #endif
                    {
                        matfile_->seekp(sizeof(double)*(p*nsotri_ + ((m*(m+1))>>1) + mstart), std::ios_base::beg);
                        matfile_->write(reinterpret_cast<const char *>(A[threadnum] + p*nm*nm + m0*nm), nwrite*sizeof(double));
                        nwrite += 1;
                    }
                }
            }
        }

    }

    if(!isinmem_)
        ResetFile();

    for(int i = 0; i < nthreads_; i++)
    {
        delete [] A[i];
        delete [] B[i];
    }

#ifdef PANACHE_TIMING
    timer_genqso.Stop();
    output::printf("  **TIMER: DFTensor Total GenQso (%s): %lu (%lu calls)\n",
                   (inmem ? "CORE" : "DISK"),
                   timer_genqso.Microseconds(),
                   timer_genqso.TimesCalled());

    // reset the other timers
    timer_getbatch_qso.Reset();
    timer_getbatch_qmo.Reset();
#endif

}



void DFTensor::SetOutputBuffer(double * buf, size_t size)
{
    outbuffer_ = buf;
    outbuffersize_ = size;


}


#ifdef PANACHE_DISKPREFETCH
int DFTensor::GetBatch_Base(int ntoget)
{
    int actualtoget = std::min(ntoget, naux_ - curq_);

    if(actualtoget <= 0)
        return 0;

    if(isinmem_)
    {
        std::copy(qso_.get() + curq_*nsotri_, qso_.get() + (curq_+actualtoget)*nsotri_, q_.get());
        curq_ += actualtoget;
    }
    else
    {
        if(curq_ == 0)
        {
            // first time through - just grab the data
            matfile_->read(reinterpret_cast<char *>(q_.get()), actualtoget*nsotri_*sizeof(double));
        }
        else
        {
            // wait for the filling of q2_ to finish
            int futureget = fill_future_.get();

            if(futureget != actualtoget)
            {
                std::cout << "Error: future returned " << futureget << " but I expected " << actualtoget << "\n";
                throw RuntimeError("Async error");
            }

            // swap
            std::swap(q_, q2_);
        }

        curq_ += actualtoget;

        // async get the next batch into q2
        int futuretoget = std::min(ntoget, naux_ - curq_);

        if(futuretoget != 0)
        {
            fill_future_ = std::async(std::launch::async,                                 // policy
                                      [] (int toget, size_t bytes, double * outbuf, std::fstream * file)  // function/lambda
                                         { 
                                            //std::cout << "PREFETCHING " << toget << " MATRICES ( " << bytes << " BYTES)...";
                                            file->read(reinterpret_cast<char *>(outbuf), bytes); 
                                            //std::cout << "Done\n";
                                            return toget;
                                         },
                                      futuretoget, futuretoget*nsotri_*sizeof(double), q2_.get(), matfile_.get()  // args to lambda
                                    );
        }
    }


    return actualtoget;
}

#else

int DFTensor::GetBatch_Base(int ntoget)
{
    // all reads should be sequential, therefore we can just start at the beginning
    //  (reset file at end of GenQ) and go from there

    int actualtoget = std::min(ntoget, naux_ - curq_);

    if(actualtoget <= 0)
        return 0;

    if(isinmem_)
        std::copy(qso_.get() + curq_*nsotri_, qso_.get() + (curq_+actualtoget)*nsotri_, q_.get());
    else
        matfile_->read(reinterpret_cast<char *>(q_.get()), actualtoget*nsotri_*sizeof(double));

    curq_ += actualtoget;

    return actualtoget;
}
#endif



int DFTensor::GetBatch_Qso(void)
{

#ifdef PANACHE_TIMING
    timer_getbatch_qso.Start();
#endif

    int nq = (outbuffersize_ / nso2_);

    // is this the first batch?
    if(curq_ == 0)
    {
        if(outbuffersize_ < nso2_)
            throw RuntimeError("Error - buffer is to small to hold even one row!");

        // allocate buffers for some q
        q_ = std::unique_ptr<double[]>(new double[nq * nsotri_]);

        #ifdef PANACHE_DISKPREFETCH
        q2_ = std::unique_ptr<double[]>(new double[nq * nsotri_]);
        #endif
    }


    // get a batch    
    int gotten;
    if(gotten = GetBatch_Base(nq))
    {
        // expand into mat
        #ifdef _OPENMP
        #pragma omp parallel for schedule(dynamic) num_threads(nthreads_)
        #endif
        for(int i = 0; i < gotten; i++)
        {
            double * my_q = q_.get() + i*nsotri_;

            int index = 0;
            for(int j = 0; j < nso_; j++)
                for(int k = 0; k <= j; k++)
                    outbuffer_[i*nso2_ + k*nso_ + j] 
                             = outbuffer_[i*nso2_ + j*nso_ + k]
                             = my_q[index++];
        }
    }


#ifdef PANACHE_TIMING
    timer_getbatch_qso.Stop();
    if(gotten == 0)
    {
        output::printf("  **TIMER: DFTensor Total GetBatch_Qso (%s): %lu (%lu calls)\n",
                       (isinmem_ ? "CORE" : "DISK"),
                       timer_getbatch_qso.Microseconds(),
                       timer_getbatch_qso.TimesCalled());
    }
#endif

    if(gotten == 0)
    {
        // free memory
        q_.reset();

        #ifdef PANACHE_DISKPREFETCH
        q2_.reset();
        #endif
    }

    return gotten;
}




int DFTensor::GetBatch_Qmo(void)
{
#ifdef PANACHE_TIMING
    timer_getbatch_qmo.Start();
#endif

    if(Cmo_ == nullptr)
        throw RuntimeError("Error - I don't have a C matrix!");

    int nq = (outbuffersize_ / nmo2_ );


    // first batch?
    if(curq_ == 0)
    {
        if(outbuffersize_ < nmo2_)
            throw RuntimeError("Error - buffer is to small to hold even one row!");

        // allocate buffers for some q
        q_ = std::unique_ptr<double[]>(new double[nq * nsotri_]);

        #ifdef PANACHE_DISKPREFETCH
        q2_ = std::unique_ptr<double[]>(new double[nq * nsotri_]);
        #endif


        q_single_ = std::unique_ptr<double[]>(new double[nq * nmo2_]);
        qc_ = std::unique_ptr<double[]>(new double[nq * nmo_ * nso_]);
    }


    int gotten;
    if(gotten = GetBatch_Base(nq))
    {
        #ifdef _OPENMP
        #pragma omp parallel for schedule(dynamic) num_threads(nthreads_)
        #endif
        for(int i = 0; i < gotten; i++)
        {
            double * my_q = q_.get() + i*nsotri_;
            double * my_q_single = q_single_.get() + i*nso2_;
            double * my_qc = qc_.get() + i*nso_*nmo_;

            // Apply the C matrices
            // Keep in mind that for the first step q_ is a (lower) part of a symmetric matrix
            // so first expand into a buffer. But we only need to fill half and we can use DSYMM
            // (we fill the lower triangle)
            int index = 0;
            for(int j = 0; j < nso_; j++)
                for(int k = 0; k <= j; k++)
                    my_q_single[j*nso_ + k] = my_q[index++];


            if(Cmo_trans_)
            {
                // matrix multiply w/ triangular matrix
                C_DSYMM('R','L',nmo_,nso_,1.0, my_q_single, nso_, Cmo_, nso_, 0.0, my_qc, nso_);

                // Then regular matrix multiplication
                C_DGEMM('N','T',nmo_, nmo_, nso_, 1.0, my_qc, nso_, Cmo_, nso_, 0.0, outbuffer_ + i*nmo2_, nmo_);
            }
            else
            {
                // matrix multiply w/ symmetric matrix
                C_DSYMM('L','L',nso_,nmo_,1.0, my_q_single, nso_, Cmo_, nmo_, 0.0, my_qc, nmo_);

                // Then regular matrix multiplication
                C_DGEMM('T','N',nmo_, nmo_, nso_, 1.0, Cmo_, nmo_, my_qc, nmo_, 0.0, outbuffer_ + i*nmo2_, nmo_);
            }
        }
    }

#ifdef PANACHE_TIMING
    timer_getbatch_qmo.Stop();
    if(gotten == 0)
    {
        output::printf("  **TIMER: DFTensor Total GetBatch_Qmo (%s): %lu (%lu calls)\n",
                       (isinmem_ ? "CORE" : "DISK"),
                       timer_getbatch_qmo.Microseconds(),
                       timer_getbatch_qmo.TimesCalled());
    }
#endif

    if(gotten == 0)
    {
        // free memory
        q_.reset();
        q_single_.reset();
        qc_.reset();

        #ifdef PANACHE_DISKPREFETCH
        q2_.reset();
        #endif

    }

    return gotten;
}


/*
int DFTensor::CalculateERI(double * qso, int qsosize, int shell1, int shell2, int shell3, int shell4, double * outbuffer, int buffersize)
{
    NOTE - MUST CHANGE THIS FUNCTION TO PROPER ORIENTATION OF THE
    QSO MATRIX - IT CAN'T BE A PLAIN DDOT ANYMORE!

    //! \todo do something with qsosize

    int nfa = primary_->shell(shell1).nfunction();
    int astart = primary_->shell(shell1).function_index();

    int nfb = primary_->shell(shell2).nfunction();
    int bstart = primary_->shell(shell2).function_index();

    int nfc = primary_->shell(shell3).nfunction();
    int cstart = primary_->shell(shell3).function_index();

    int nfd = primary_->shell(shell4).nfunction();
    int dstart = primary_->shell(shell4).function_index();

    int nint = nfa * nfb * nfc * nfd;

    int nbf = primary_->nbf();
    int naux = auxiliary_->nbf();


    if(nint > buffersize)
        throw RuntimeError("Error - ERI buffer not large enough!");

    int bufindex = 0;

    //!\todo replace with DGEMM?
    for(int a = 0; a < nfa; a++)
        for(int b = 0; b < nfb; b++)
            for(int c = 0; c < nfc; c++)
                for(int d = 0; d < nfd; d++)
                {
                    outbuffer[bufindex] = C_DDOT(naux,
                                                 qso + (astart+a)*nbf*naux+(bstart+b)*naux, 1,
                                                 qso + (cstart+c)*nbf*naux+(dstart+d)*naux, 1);
                    bufindex++;
                }

    return nint;
}


int DFTensor::CalculateERIMulti(double * qso, int qsosize,
                                int shell1, int nshell1,
                                int shell2, int nshell2,
                                int shell3, int nshell3,
                                int shell4, int nshell4,
                                double * outbuffer, int buffersize)
{
    NOTE - MUST CHANGE THIS FUNCTION TO PROPER ORIENTATION OF THE
    QSO MATRIX - IT CAN'T BE A PLAIN DDOT ANYMORE!

    //! \todo do something with qsosize
    int nint = 0;

    int nbf = primary_->nbf();
    int naux = auxiliary_->nbf();

    int bufindex = 0;

    for(int i = 0; i < nshell1; i++)
    {
        int nfa = primary_->shell(shell1+i).nfunction();
        int astart = primary_->shell(shell1+i).function_index();

        for(int a = 0; a < nfa; a++)
        {
            for(int j = 0; j < nshell2; j++)
            {
                int nfb = primary_->shell(shell2+j).nfunction();
                int bstart = primary_->shell(shell2+j).function_index();

                for(int b = 0; b < nfb; b++)
                {
                    for(int k = 0; k < nshell3; k++)
                    {
                        int nfc = primary_->shell(shell3+k).nfunction();
                        int cstart = primary_->shell(shell3+k).function_index();

                        for(int c = 0; c < nfc; c++)
                        {
                            for(int l = 0; l < nshell4; l++)
                            {
                                int nfd = primary_->shell(shell4+l).nfunction();
                                int dstart = primary_->shell(shell4+l).function_index();

                                nint += nfd;

                                if(nint > buffersize)
                                    throw RuntimeError("Error - ERI buffer not large enough!");

                                for(int d = 0; d < nfd; d++)
                                {
                                    outbuffer[bufindex] = C_DDOT(naux,
                                                                 qso + (astart+a)*nbf*naux+(bstart+b)*naux, 1,
                                                                 qso + (cstart+c)*nbf*naux+(dstart+d)*naux, 1);
                                    bufindex++;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    return nint;
}
*/


// note - passing by value for the vector
static void Reorder(std::vector<unsigned short> order, std::vector<double *> pointers,
                    reorder::MemorySwapper & sf)
{
    size_t size = order.size();

    // original order is 1 2 3 4 5 6....
    std::vector<unsigned short> currentorder(size);

    for(int i = 0; i < size; i++)
        currentorder[i] = i+1;

    for(int i = 0; i < size; i++)
    {
        // find the index in the current order
        size_t cindex = 0;
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
    for(int i = 0; i < size; i++)
    {
        if(currentorder[i] != order[i])
            throw RuntimeError("Reordering failed!");
    }
}



/*
void DFTensor::ReorderQ(double * qso, int qsosize, const reorder::Orderings & order)
{
    using namespace reorder;
    using std::placeholders::_1;
    using std::placeholders::_2;

    int nso = primary_->nbf();
    int nq = auxiliary_->nbf();

    if((nq * nso * nso) != qsosize)
        throw RuntimeError("Incompatible Qso matrix in ReorderQ");

    // Dimensions on Q:
    // nso * nso * nq


    TotalMemorySwapper sf1(nq);  // swaps rows
    LimitedMemorySwapper sf2(nso*nq, 1e7); // swaps 'tables' (limited to ~80MB extra memory)

    std::vector<PointerMap> vpm;

    //go through what would need to be changed in the primary basis
    for(int i = 0; i < primary_->nshell(); i++)
    {
        const GaussianShell & s = primary_->shell(i);
        if(order.NeedsReordering(s.am()))
            vpm.push_back(PointerMap(s.function_index(), order.GetOrder(s.am())));
    }


    std::vector<double *> pointers(primary_->max_function_per_shell());


    // for each i
    for(size_t i = 0; i < nso; i++)
    {
        // Swap rows
        for(auto & it : vpm)
        {
            size_t ntoswap = it.order.size();
            size_t start = i*nso*nq;

            for(size_t n = 0; n < ntoswap; n++)
                pointers[n] = qso + start + (it.start+n)*nq;

            Reorder(it.order, pointers, sf1);

        }

    }


    // swap 'tables'
    for(auto & it : vpm)
    {
        size_t ntoswap = it.order.size();

        for(size_t n = 0; n < ntoswap; n++)
            pointers[n] = qso + (it.start+n)*nso*nq;

        Reorder(it.order, pointers, sf2);

    }

}

void DFTensor::ReorderQ_GAMESS(double * qso, int qsosize)
{
    reorder::GAMESS_Ordering go;
    ReorderQ(qso, qsosize, go);
}
*/


void DFTensor::OpenFile(void)
{
    if(filename_ == "")
        throw RuntimeError("Error - no file specified!");

    // ok to call if it hasn't been opened yet
    CloseFile();

    matfile_ = std::unique_ptr<std::fstream>(new std::fstream(filename_.c_str(), std::fstream::in |
               std::fstream::out |
               std::fstream::binary |
               std::fstream::trunc));

    if(!matfile_->is_open())
        throw RuntimeError(filename_);

    // enable exceptions
    matfile_->exceptions(std::fstream::failbit | std::fstream::badbit | std::fstream::eofbit);
}

void DFTensor::CloseFile(void)
{
    if(matfile_)
    {
        if(matfile_->is_open())
            matfile_->close();
        matfile_.reset();
    }
}


void DFTensor::ResetFile(void)
{
    matfile_->seekg(0);
    matfile_->seekp(0);
    curq_ = 0;
}

void DFTensor::ResetBatches(void)
{
    curq_ = 0;
    if(!isinmem_)
        ResetFile();
}

}











