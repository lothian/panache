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

namespace panache
{

DFTensor::DFTensor(std::shared_ptr<BasisSet> primary,
                   std::shared_ptr<BasisSet> auxiliary)
    : primary_(primary), auxiliary_(auxiliary), filename_("/home/ben/Test.mat")
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

    // allocate the buffers for C^T Q C
    qc_ = std::unique_ptr<double[]>(new double[nso_*nmo_]);
    q_ = std::unique_ptr<double[]>(new double[nso2_]);
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

    std::shared_ptr<TwoBodyAOInt> eri = GetERI(auxiliary_,zero,primary_,primary_);

    const double* buffer = eri->buffer();

    isinmem_ = inmem; // store this so we know later

    qso_.reset();
    curq_ = 0;

    if(!inmem)
    {
        OpenFile();
        ResetFile();

        double * A = new double[naux_*maxpershell2];
        double * B = new double[naux_*maxpershell2];

        for (int M = 0; M < primary_->nshell(); M++)
        {
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

                    eri->compute_shell(P,0,M,N);

                    for (int p = pstart, index = 0; p < pend; p++)
                    {
                        for (int m = 0; m < nm; m++)
                        {
                            for (int n = 0; n < nn; n++, index++)
                            {
                                B[p*nm*nn + m*nn + n] = buffer[index];
                            }
                        }
                    }
                }

                // we now have a set of columns of B, although "condensed"
                // we can do a DGEMM with J
                C_DGEMM('N','N',naux_, nm*nn, naux_, 1.0, Jp[0], naux_, B, nm*nn, 0.0,
                        A, nm*nn);


                // write to disk

                //! \todo rearrange to that writes are more sequential?
                for (int p = 0; p < naux_; p++)
                {
                    for (int m0 = 0, m = mstart; m < mend; m0++, m++)
                    {
                        matfile_->seekp(sizeof(double)*(p*nsotri_ + ((m*(m+1))>>1) + nstart), std::ios_base::beg);
                        matfile_->write(reinterpret_cast<const char *>(A + p*nm*nn + m0*nn), nn*sizeof(double));
                    }

                }
            }


            // Special Case: N = M
            for (int P = 0; P < auxiliary_->nshell(); P++)
            {
                int np = auxiliary_->shell(P).nfunction();
                int pstart = auxiliary_->shell(P).function_index();
                int pend = pstart + np;

                eri->compute_shell(P,0,M,M);

                for (int p = pstart, index = 0; p < pend; p++)
                {
                    for (int m = 0; m < nm; m++)
                    {
                        for (int n = 0; n < nm; n++, index++)
                        {
                            B[p*nm*nm + m*nm + n] = buffer[index];
                        }
                    }
                }
            }

            // we now have a set of columns of B, although "condensed"
            // we can do a DGEMM with J
            C_DGEMM('N','N',naux_, nm*nm, naux_, 1.0, Jp[0], naux_, B, nm*nm, 0.0,
                    A, nm*nm);


            // write to disk
            //! \todo rearrange to that writes are more sequential?
            for (int p = 0; p < naux_; p++)
            {
                int nwrite = 1;
                for (int m0 = 0, m = mstart; m < mend; m0++, m++)
                {
                    matfile_->seekp(sizeof(double)*(p*nsotri_ + ((m*(m+1))>>1) + mstart), std::ios_base::beg);
                    matfile_->write(reinterpret_cast<const char *>(A + p*nm*nm + m0*nm), nwrite*sizeof(double));
                    nwrite += 1;
                }

            }

        }

        delete [] A;
        delete [] B;

        ResetFile();

    }
    else
    {
        // only store the triangular parts!
        // (Actually only the lower triangle here, including the diagonal

        // Note - the case where N=M is split out to avoid having an if (n == m) or
        // something similar inside the 3-fold loop

        double * B = new double[naux_*nsotri_];
        qso_ = std::unique_ptr<double[]>(new double[naux_*nsotri_]);

        for (int M = 0; M < primary_->nshell(); M++)
        {
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

                    eri->compute_shell(P,0,M,N);

                    for (int p = pstart, index = 0; p < pend; p++)
                    {
                        for (int m = mstart; m < mend; m++)
                        {
                            for (int n = nstart; n < nend; n++, index++)
                            {
                                // note - because of the setup above, M > N, and therefore
                                //  m > n
                                B[p*nsotri_ + ((m*(m+1))>>1) + n] = buffer[index];
                            }
                        }
                    }
                }
            }

            // special case: N == M
            for (int P = 0; P < auxiliary_->nshell(); P++)
            {
                int np = auxiliary_->shell(P).nfunction();
                int pstart = auxiliary_->shell(P).function_index();

                eri->compute_shell(P,0,M,M);

                for (int p = 0; p < np; p++)
                {
                    for (int m0 = 0, m = mstart; m < mend; m++, m0++)
                    {
                        for (int n0 = 0, n = mstart; n <= m; n++, n0++)
                            B[(p + pstart)*nsotri_ + ((m*(m+1))>>1) + n] = buffer[p*nm*nm + m0*nm + n0];
                    }
                }
            }
        }

        C_DGEMM('N','N',naux_, nsotri_, naux_, 1.0, Jp[0], naux_, B, nsotri_, 0.0,
                qso_.get(), nsotri_);

        delete [] B;
    }

#ifdef PANACHE_TIMING
    timer_genqso.Stop();
    output::printf("  **TIMER: DFTensor Total GenQso (%s): %lu\n",
                   (inmem ? "CORE" : "DISK"), timer_genqso.Microseconds());
#endif

}

int DFTensor::GetBatch_Base(double * mat, size_t size)
{
    if(size < nso2_)
        throw RuntimeError("Error - buffer is to small to hold even one row!");

    int nq = (size / nso2_ );
    int toget = std::min(nq, (naux_ - curq_));

    // all reads should be sequential, therefore we can just start at the beginning
    //  (reset file at end of GenQ) and go from there

    if(isinmem_)
    {
        int start = curq_ * nsotri_;

        for(int q = 0; q < toget; q++)
        {
            // expand the triangular matrix
            for(int i = 0; i < nso_; i++)
            for(int j = 0; j <= i; j++)
            {
                mat[q*nso2_ + i*nso_ + j] = mat[q*nso2_ + j*nso_ + i]
                                          = qso_[start + ((i*(i+1))>>1) + j];
            }

            start += nsotri_;
        }
    }
    else
    {
        double * buf = new double[toget*nsotri_];

        matfile_->read(reinterpret_cast<char *>(buf), toget*nsotri_*sizeof(double));
        

        for(int q = 0; q < toget; q++)
        {
            // expand the triangular matrix
            for(int i = 0; i < nso_; i++)
            for(int j = 0; j <= i; j++)
            {
                mat[q*nso2_ + i*nso_ + j] = mat[q*nso2_ + j*nso_ + i]
                                          = buf[q*nsotri_ + ((i*(i+1))>>1) + j];
            }
        }

        delete [] buf;
    }


    curq_+= toget;


    return toget;
}


int DFTensor::GetBatch_Qso(double * mat, size_t size)
{
    return GetBatch_Base(mat, size);
}

int DFTensor::GetBatch_Qmo(double * mat, size_t size)
{
    if(Cmo_ == nullptr)
        throw RuntimeError("Error - I don't have a C matrix!");

    if(size < nmo2_)
        throw RuntimeError("Error - buffer is to small to hold even one row!");

    int nq = (size / nmo2_ );
    int toget = std::min(nq, (naux_ - curq_));

    for(int i = 0; i < nq; i++)
    {
        // get one q - should always return 1 here
        if(GetBatch_Base(q_.get(), nso2_))
        {
            // Apply the C matrices
            if(Cmo_trans_)
            {
                C_DGEMM('N','N',nmo_, nso_, nso_, 1.0, Cmo_, nmo_, q_.get(), nso_, 0.0, qc_.get(), nso_);
                C_DGEMM('N','T',nmo_, nmo_, nso_, 1.0, qc_.get(), nso_, Cmo_, nmo_, 0.0, mat + i*nmo2_, nmo_);
            }
            else
            {
                C_DGEMM('T','N',nmo_, nso_, nso_, 1.0, Cmo_, nmo_, q_.get(), nso_, 0.0, qc_.get(), nso_);
                C_DGEMM('N','N',nmo_, nmo_, nso_, 1.0, qc_.get(), nso_, Cmo_, nmo_, 0.0, mat + i*nmo2_, nmo_);
            }
        }
    }

    return toget;
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




