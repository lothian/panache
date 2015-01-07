/*! \file
 *  \brief Fortran interface to the PANACHE library
 *  \author Benjamin Pritchard (ben@bennyp.org)
 */


#include <iostream>
#include <cstring> // strncpy

#include "panache/c_interface.h"
#include "panache/Output.h"


extern "C" {


    /*!
     * \brief Initializes a new density-fitting calculation
     *
     * Sets up the basis set information and returns a handle that
     * is used to identify this particular calculation.
     *
     * Information passed in is copied, so any dynamic arrays, etc, can be safely deleted afterwards
     *
     * \note Basis set coefficients should NOT be normalized
     *
     * \param [in] ncenters    The number of basis function centers
     * \param [in] xyz         Coordinates of the basis function centers. In order:
     *                         (x1, y1, z1, x2, y2, z2, ..., xN, yN, zN)
     * \param [in] symbols     Atomic symbols for each center, as a set of \p ncenters strings of length \p symbollen
     * \param [in] symbollen   Length of each symbol in \p symbols
     * \param [in] primary_nshellspercenter  Number of shells on each center for the primary basis.
     *                                       Expected to be of length ncenters.
     * \param [in] primary_am  Angular momentum of each shell (s = 0, p = 1, etc) in the primary basis. 
     *                         Length should be the sum of primary_nshellspercenter.
     * \param [in] primary_is_pure  Whether each shell is pure/spherical or not (primary basis).
     *                              Length should be the sum of primary_nshellspercenter.
     * \param [in] primary_nprimpershell  Number of primitives in each shell of the primary basis. 
     *                                    Length should be the sum of primary_nshellspercenter.
     * \param [in] primary_exp  All exponents for all shells of the primary basis. 
     *                          Length should be the sum of primary_nprimpershell, with grouping
     *                          by shell.
     * \param [in] primary_coef All basis function coefficients for all shells of the primary basis. 
     *                          Length should be the sum of primary_nprimpershell, with grouping
     *                          by shell.
     *
     * \param [in] aux_nshellspercenter  Number of shells on each center for the auxiliary basis.
     *                                   Expected to be of length ncenters.
     * \param [in] aux_am  Angular momentum of each shell (s = 0, p = 1, etc) in the auxiliary basis. 
     *                     Length should be the sum of aux_nshellspercenter.
     * \param [in] aux_is_pure  Whether each shell is pure/spherical or not (auxiliary basis).
     *                          Length should be the sum of aux_nshellspercenter.
     * \param [in] aux_nprimpershell  Number of primitives in each shell of the auxiliary basis. 
     *                                Length should be the sum of aux_nshellspercenter.
     * \param [in] aux_exp  All exponents for all shells of the auxiliary basis. 
     *                      Length should be the sum of aux_nprimpershell, with grouping
     *                      by shell.
     * \param [in] aux_coef All basis function coefficients for all shells of the auxiliary basis. 
     *                      Length should be the sum of aux_nprimpershell, with grouping
     *                      by shell.
     * \param [in] directory A full path to a file to be used if storing matrices to disk.
     *                       Not referenced if the disk is not used. Should not be set to "NULL", but
     *                       may be set to an empty string if disk is not to be used.
     *                       If used, any existing files will be overwritten.
     * \param [in] directorylen Actual length of \p filename
     * \param [in] bsorder Basis function ordering flag
     * \param [in] nthreads Number of threads to use
     *
     * \param [out] dfhandle A handle representing this particular density-fitting calculation.
     */
    void panachef_dfinit_(int_t * ncenters,
                       double * xyz, char * symbols, int_t * symbollen,
                       int_t * primary_nshellspercenter, int_t * primary_am, int_t * primary_is_pure,
                       int_t * primary_nprimpershell, double * primary_exp, double * primary_coef,
                       int_t * aux_nshellspercenter, int_t * aux_am, int_t * aux_is_pure,
                       int_t * aux_nprimpershell, double * aux_exp, double * aux_coef,
                       const char * directory, int_t * directorylen, int_t * bsorder, int_t * nthreads,
                       int_t * dfhandle)
    {
        // Make molecule struct
        C_AtomCenter * atoms = new C_AtomCenter[*ncenters];

        char ** csymbols = new char*[*ncenters];
        for(int_t i = 0; i < *ncenters; i++)
        {
            csymbols[i] = new char[*symbollen+1];
            for(int_t j = 0; j < ((*symbollen)+1); j++)
                csymbols[i][j] = '\0';
        }


        // symbols should be basically a long, space-delimited string
        // length is ncenters * 4, but is NOT null delimited.
        // fortran inserts spaces instead.

        for(int_t i = 0; i < *ncenters; i++)
        {
            for(int_t j = 0; j < *symbollen; j++)
            {
                char c = symbols[i * (*symbollen) + j];
                if(c != ' ')
                    csymbols[i][j] = c;
            }

            atoms[i].symbol = csymbols[i]; 

            atoms[i].center[0] = xyz[3*i];
            atoms[i].center[1] = xyz[3*i+1];
            atoms[i].center[2] = xyz[3*i+2];
        }

        int_t p_nshell = 0;
        int_t a_nshell = 0;
        for(int_t i = 0; i < *ncenters; i++)
        {
            p_nshell += primary_nshellspercenter[i];
            a_nshell += aux_nshellspercenter[i];
        }

        C_ShellInfo * primary_shells = new C_ShellInfo[p_nshell];
        C_ShellInfo * aux_shells = new C_ShellInfo[a_nshell];

        int_t prim_count = 0;
        for(int_t i = 0; i < p_nshell; i++)
        {
            primary_shells[i].nprim   = primary_nprimpershell[i];
            primary_shells[i].am      = primary_am[i];
            primary_shells[i].ispure  = primary_is_pure[i];
            primary_shells[i].exp     = new double[primary_shells[i].nprim];
            primary_shells[i].coef    = new double[primary_shells[i].nprim];
            for(int_t j = 0; j < primary_shells[i].nprim; j++)
            {
                primary_shells[i].exp[j] = primary_exp[prim_count];
                primary_shells[i].coef[j] = primary_coef[prim_count++];
            }
        }

        prim_count = 0;
        for(int_t i = 0; i < a_nshell; i++)
        {
            aux_shells[i].nprim   = aux_nprimpershell[i];
            aux_shells[i].am      = aux_am[i];
            aux_shells[i].ispure  = aux_is_pure[i];
            aux_shells[i].exp     = new double[aux_shells[i].nprim];
            aux_shells[i].coef    = new double[aux_shells[i].nprim];
            for(int_t j = 0; j < aux_shells[i].nprim; j++)
            {
                aux_shells[i].exp[j] = aux_exp[prim_count];
                aux_shells[i].coef[j] = aux_coef[prim_count++];
            }
        }

        // create a real, null-terminated c string
        char * mdir = new char[*directorylen+1];
        strncpy(mdir, directory, *directorylen);
        mdir[*directorylen] = '\0';

        *dfhandle = panache_dfinit(*ncenters, atoms,
                                 primary_nshellspercenter, primary_shells,
                                 aux_nshellspercenter, aux_shells, mdir, *bsorder, *nthreads);

        // Free memory
        for(int_t i = 0; i < p_nshell; i++)
        {
            delete [] primary_shells[i].exp;
            delete [] primary_shells[i].coef;
        }

        for(int_t i = 0; i < a_nshell; i++)
        {
            delete [] aux_shells[i].exp;
            delete [] aux_shells[i].coef;
        }

        delete [] primary_shells;
        delete [] aux_shells;

        delete [] atoms;

        for(int_t i = 0; i < *ncenters; i++)
            delete [] csymbols[i];
        delete [] csymbols;


    }


    /*!
     * \brief Initializes a new density-fitting calculation using an auxiliary basis set file
     *
     * Sets up the basis set information and returns a handle that
     * is used to identify this particular calculation.
     *
     * Information passed in is copied, so any dynamic arrays, etc, can be safely deleted afterwards
     *
     * \note Basis set coefficients should NOT be normalized
     *
     * \param [in] ncenters    The number of basis function centers
     * \param [in] xyz         Coordinates of the basis function centers. In order:
     *                         (x1, y1, z1, x2, y2, z2, ..., xN, yN, zN)
     * \param [in] symbols     Atomic symbols for each center, as a set of \p ncenters strings of length \p symbollen
     * \param [in] symbollen   Length of each symbol in \p symbols
     *
     * \param [in] primary_nshellspercenter  Number of shells on each center for the primary basis.
     *                                       Expected to be of length ncenters.
     * \param [in] primary_am  Angular momentum of each shell (s = 0, p = 1, etc) in the primary basis. 
     *                         Length should be the sum of primary_nshellspercenter.
     * \param [in] primary_is_pure  Whether each shell is pure/spherical or not (primary basis).
     *                              Length should be the sum of primary_nshellspercenter.
     * \param [in] primary_nprimpershell  Number of primitives in each shell of the primary basis. 
     *                                    Length should be the sum of primary_nshellspercenter.
     * \param [in] primary_exp  All exponents for all shells of the primary basis. 
     *                          Length should be the sum of primary_nprimpershell, with grouping
     *                          by shell.
     * \param [in] primary_coef All basis function coefficients for all shells of the primary basis. 
     *                          Length should be the sum of primary_nprimpershell, with grouping
     *                          by shell.
     * \param [in] auxfilename A full path to a file containing the auxiliary basis set (in Gaussian94 format)
     * \param [in] auxfilenamelen Actual length of \p auxfilename
     *
     * \param [in] directory A full path to a file to be used if storing matrices to disk.
     *                       Not referenced if the disk is not used. Should not be set to "NULL", but
     *                       may be set to an empty string if disk is not to be used.
     * \param [in] directorylen Actual length of \p filename
     * \param [in] bsorder Basis function ordering flag
     * \param [in] nthreads Number of threads to use
     *
     * \param [out] dfhandle A handle representing this particular density-fitting calculation.
     */
    void panachef_dfinit2_(int_t * ncenters,
                        double * xyz, char * symbols, int_t * symbollen,
                        int_t * primary_nshellspercenter, int_t * primary_am, int_t * primary_is_pure,
                        int_t * primary_nprimpershell, double * primary_exp, double * primary_coef,
                        const char * auxfilename, int_t * auxfilenamelen, const char * directory,
                        int_t * directorylen, int_t * bsorder, int_t * nthreads, int_t * dfhandle)
    {
        // DUMP
        /*
        std::cout << "ncenters: " << *ncenters << "\n";
        std::cout << "XYZ: \n";
        int cc = 0;
        for(int i = 0; i < 3; i++)
        {
            for(int j = 0; j < (*ncenters); j++)
                std::cout << xyz[cc++] << "   ";
            std::cout << "\n";
        }
        std::cout << "symbol len: " << *symbollen << "\n";
        std::cout << "Symbols: \n";
        cc = 0;
        for(int i = 0; i < *ncenters; i++)
        {
            std::cout << "\"";
            for(int j = 0; j < *symbollen; j++)
                std::cout << symbols[cc++];
            std::cout << "\"\n";
        }
        */

        // Make molecule struct
        C_AtomCenter * atoms = new C_AtomCenter[*ncenters];

        char ** csymbols = new char*[*ncenters];
        for(int_t i = 0; i < *ncenters; i++)
        {
            csymbols[i] = new char[*symbollen+1];
            for(int_t j = 0; j < ((*symbollen)+1); j++)
                csymbols[i][j] = '\0';
        }

        // symbols should be basically a long, space-delimited string
        // length is ncenters * 4, but is NOT null delimited.
        // fortran inserts spaces instead.
        // So the extra element from above guarentee room for a null
        // character


        for(int_t i = 0; i < *ncenters; i++)
        {
            for(int_t j = 0; j < *symbollen; j++)
            {
                char c = symbols[i * (*symbollen) + j];
                if(c != ' ')
                    csymbols[i][j] = c;
            }

            atoms[i].symbol = csymbols[i]; 

            atoms[i].center[0] = xyz[3*i];
            atoms[i].center[1] = xyz[3*i+1];
            atoms[i].center[2] = xyz[3*i+2];
            //std::cout << "Adding atom " << i << "(" << atoms[i].symbol << ") with center:\n";
            //std::cout << atoms[i].center[0] << "  " << atoms[i].center[1] << "  " << atoms[i].center[2] << "\n";
        }

        int_t p_nshell = 0;
        for(int_t i = 0; i < *ncenters; i++)
        {
            p_nshell += primary_nshellspercenter[i];
            //std::cout << "NSHELLPERCENTER " << i << " = " << primary_nshellspercenter[i] << "\n";
        }

        C_ShellInfo * primary_shells = new C_ShellInfo[p_nshell];

        int_t prim_count = 0;
        /*
        std::cout << "P_NSHELL: " << p_nshell << "\n";
        std::cout << "NPRIMPERSHELL: \n";
        for(int i = 0; i < p_nshell; i++)
            std::cout << primary_nprimpershell[i] << "  ";
        std::cout << "\n";
        */

        for(int_t i = 0; i < p_nshell; i++)
        {
            //std::cout << "shell " << i << " nprim " << primary_nprimpershell[i]
            //          << " am " << primary_am[i] << " pure? " << primary_is_pure[i] << "\n";
            primary_shells[i].nprim   = primary_nprimpershell[i];
            primary_shells[i].am      = primary_am[i];
            primary_shells[i].ispure  = primary_is_pure[i];
            primary_shells[i].exp     = new double[primary_shells[i].nprim];
            primary_shells[i].coef    = new double[primary_shells[i].nprim];
            for(int_t j = 0; j < primary_shells[i].nprim; j++)
            {
                primary_shells[i].exp[j] = primary_exp[prim_count];
                primary_shells[i].coef[j] = primary_coef[prim_count++];
            }
        }

        /*
        std::cout << "PRIMCOUNT: " << prim_count << "\n";
        std::cout << "exp, coef\n";
        for(int i = 0; i < prim_count; i++)
            std::cout << primary_exp[i] << "  " << primary_coef[i] << "\n";
        std::cout << "\n";
        */

        // create a real, null-terminated c string
        char * cfname = new char[*auxfilenamelen+1];
        char * mdir = new char[*directorylen+1];
        strncpy(cfname, auxfilename, *auxfilenamelen);
        strncpy(mdir, directory, *directorylen);
        cfname[*auxfilenamelen] = '\0';
        mdir[*directorylen] = '\0';

        *dfhandle = panache_dfinit2(*ncenters, atoms,
                                  primary_nshellspercenter, primary_shells,
                                  cfname, mdir, *bsorder, *nthreads);

        // Free memory
        delete [] cfname;
        delete [] mdir;

        for(int_t i = 0; i < p_nshell; i++)
        {
            delete [] primary_shells[i].exp;
            delete [] primary_shells[i].coef;
        }

        delete [] primary_shells;

        delete [] atoms;

        for(int_t i = 0; i < *ncenters; i++)
            delete [] csymbols[i];
        delete [] csymbols;


    }


    /*!
     * \brief Initializes a new density-fitting calculation using an auxiliary basis set file
     *
     * Sets up the basis set information and returns a handle that
     * is used to identify this particular calculation.
     *
     * Information passed in is copied, so any dynamic arrays, etc, can be safely deleted afterwards
     *
     * \note Basis set coefficients should NOT be normalized
     *
     * \param [in] ncenters    The number of basis function centers
     * \param [in] xyz         Coordinates of the basis function centers. In order:
     *                         (x1, y1, z1, x2, y2, z2, ..., xN, yN, zN)
     * \param [in] symbols     Atomic symbols for each center, as a set of \p ncenters strings of length \p symbollen
     * \param [in] symbollen   Length of each symbol in \p symbols
     *
     * \param [in] primary_nshellspercenter  Number of shells on each center for the primary basis.
     *                                       Expected to be of length ncenters.
     * \param [in] primary_am  Angular momentum of each shell (s = 0, p = 1, etc) in the primary basis. 
     *                         Length should be the sum of primary_nshellspercenter.
     * \param [in] primary_is_pure  Whether each shell is pure/spherical or not (primary basis).
     *                              Length should be the sum of primary_nshellspercenter.
     * \param [in] primary_nprimpershell  Number of primitives in each shell of the primary basis. 
     *                                    Length should be the sum of primary_nshellspercenter.
     * \param [in] primary_exp  All exponents for all shells of the primary basis. 
     *                          Length should be the sum of primary_nprimpershell, with grouping
     *                          by shell.
     * \param [in] primary_coef All basis function coefficients for all shells of the primary basis. 
     *                          Length should be the sum of primary_nprimpershell, with grouping
     *                          by shell.
     *
     * \param [in] delta     Maximum error in the Cholesky procedure
     * \param [in] directory A full path to a file to be used if storing matrices to disk.
     *                       Not referenced if the disk is not used. Should not be set to "NULL", but
     *                       may be set to an empty string if disk is not to be used.
     * \param [in] directorylen Actual length of \p filename
     * \param [in] bsorder Basis function ordering flag
     * \param [in] nthreads Number of threads to use
     *
     * \param [out] dfhandle A handle representing this particular density-fitting calculation.
     */
    void panachef_chinit_(int_t * ncenters,
                        double * xyz, char * symbols, int_t * symbollen,
                        int_t * primary_nshellspercenter, int_t * primary_am, int_t * primary_is_pure,
                        int_t * primary_nprimpershell, double * primary_exp, double * primary_coef,
                        double * delta, const char * directory,
                        int_t * directorylen, int_t * bsorder, int_t * nthreads, int_t * dfhandle)
    {
        // DUMP
        /*
        std::cout << "ncenters: " << *ncenters << "\n";
        std::cout << "XYZ: \n";
        int cc = 0;
        for(int i = 0; i < 3; i++)
        {
            for(int j = 0; j < (*ncenters); j++)
                std::cout << xyz[cc++] << "   ";
            std::cout << "\n";
        }
        std::cout << "symbol len: " << *symbollen << "\n";
        std::cout << "Symbols: \n";
        cc = 0;
        for(int i = 0; i < *ncenters; i++)
        {
            std::cout << "\"";
            for(int j = 0; j < *symbollen; j++)
                std::cout << symbols[cc++];
            std::cout << "\"\n";
        }
        */

        // Make molecule struct
        C_AtomCenter * atoms = new C_AtomCenter[*ncenters];

        char ** csymbols = new char*[*ncenters];
        for(int_t i = 0; i < *ncenters; i++)
        {
            csymbols[i] = new char[*symbollen+1];
            for(int_t j = 0; j < ((*symbollen)+1); j++)
                csymbols[i][j] = '\0';
        }

        // symbols should be basically a long, space-delimited string
        // length is ncenters * 4, but is NOT null delimited.
        // fortran inserts spaces instead.
        // So the extra element from above guarentee room for a null
        // character


        for(int_t i = 0; i < *ncenters; i++)
        {
            for(int_t j = 0; j < *symbollen; j++)
            {
                char c = symbols[i * (*symbollen) + j];
                if(c != ' ')
                    csymbols[i][j] = c;
            }

            atoms[i].symbol = csymbols[i]; 

            atoms[i].center[0] = xyz[3*i];
            atoms[i].center[1] = xyz[3*i+1];
            atoms[i].center[2] = xyz[3*i+2];
            //std::cout << "Adding atom " << i << "(" << atoms[i].symbol << ") with center:\n";
            //std::cout << atoms[i].center[0] << "  " << atoms[i].center[1] << "  " << atoms[i].center[2] << "\n";
        }

        int_t p_nshell = 0;
        for(int_t i = 0; i < *ncenters; i++)
        {
            p_nshell += primary_nshellspercenter[i];
            //std::cout << "NSHELLPERCENTER " << i << " = " << primary_nshellspercenter[i] << "\n";
        }

        C_ShellInfo * primary_shells = new C_ShellInfo[p_nshell];

        int_t prim_count = 0;
        /*
        std::cout << "P_NSHELL: " << p_nshell << "\n";
        std::cout << "NPRIMPERSHELL: \n";
        for(int i = 0; i < p_nshell; i++)
            std::cout << primary_nprimpershell[i] << "  ";
        std::cout << "\n";
        */

        for(int_t i = 0; i < p_nshell; i++)
        {
            //std::cout << "shell " << i << " nprim " << primary_nprimpershell[i]
            //          << " am " << primary_am[i] << " pure? " << primary_is_pure[i] << "\n";
            primary_shells[i].nprim   = primary_nprimpershell[i];
            primary_shells[i].am      = primary_am[i];
            primary_shells[i].ispure  = primary_is_pure[i];
            primary_shells[i].exp     = new double[primary_shells[i].nprim];
            primary_shells[i].coef    = new double[primary_shells[i].nprim];
            for(int_t j = 0; j < primary_shells[i].nprim; j++)
            {
                primary_shells[i].exp[j] = primary_exp[prim_count];
                primary_shells[i].coef[j] = primary_coef[prim_count++];
            }
        }

        /*
        std::cout << "PRIMCOUNT: " << prim_count << "\n";
        std::cout << "exp, coef\n";
        for(int i = 0; i < prim_count; i++)
            std::cout << primary_exp[i] << "  " << primary_coef[i] << "\n";
        std::cout << "\n";
        */

        // create a real, null-terminated c string
        char * mdir = new char[*directorylen+1];
        strncpy(mdir, directory, *directorylen);
        mdir[*directorylen] = '\0';

        *dfhandle = panache_chinit(*ncenters, atoms,
                                  primary_nshellspercenter, primary_shells,
                                  *delta, mdir, *bsorder, *nthreads);

        // Free memory
        delete [] mdir;

        for(int_t i = 0; i < p_nshell; i++)
        {
            delete [] primary_shells[i].exp;
            delete [] primary_shells[i].coef;
        }

        delete [] primary_shells;

        delete [] atoms;

        for(int_t i = 0; i < *ncenters; i++)
            delete [] csymbols[i];
        delete [] csymbols;


    }

    


    /*!
     * \brief Clean up a particular density-fitting calculation and free memory
     *
     * You should not attempt to use the handle afterwards
     *
     * \param [in] df_handle A handle (returned from an init function) for the DF
     *                       calculation to be cleaned up
     */
    void panachef_cleanup_(int_t * df_handle)
    {
        panache_cleanup(*df_handle);
    }




    /*!
     * \brief Cleans up all density fitting calculations
     *
     * All handles are invalid after this point
     */
    void panachef_cleanup_all_(void)
    {
        panache_cleanup_all();
    }




    /*!
     * \brief Sets the text output of PANACHE to stout
     */ 
    void panachef_stdout_(void)
    {
        panache::output::SetOutput(&std::cout);
    }


    /*!
     * \brief Sets the C matrix (so-ao matrix) for use in generating Qmo, etc
     *
     * The matrix is expected be nso x nmo (MOs in the columns) in column-major order.
     * If it is nmo x nso, or the matrix is in row major order, set \p cmo_is_trans.
     *
     * \note This is different from panache_setcmatrix(), as fortran column-major order
     * matrices are handled automatically.
     *
     * The matrix is copied by the PANACHE code, so it can be safely deleted or otherwise
     * changed after calling this function.
     *
     * \param [in] df_handle A handle (returned from an init function) for this DF calculation
     * \param [in] cmo Pointer to a nso x nmo matrix representing the MO coefficients
     * \param [in] nmo Number of MOs in this C matrix
     * \param [in] cmo_is_trans Set to non-zero if the matrix is the transpose (nmo x nso) or
     *                          is in row-major order.
     */
    void panachef_setcmatrix_(int_t * df_handle, double * cmo, int_t * nmo, int_t * cmo_is_trans)
    {
        if(*cmo_is_trans)
          panache_setcmatrix(*df_handle, cmo, *nmo, 0);
        else
          panache_setcmatrix(*df_handle, cmo, *nmo, 1);
    }



    /*!
     * \brief Sets the number of occupied and virtual orbitals.
     *
     * Number of virtual orbitals is taken to be the remainder after the occupied.
     * Used by Qov, etc.
     *
     * Frozen orbitals should be counted in \p nocc.
     *
     * \note You must set the C Matrix first before calling (see panache_setcmatrix())
     *
     * \param [in] df_handle A handle (returned from an init function) for the DF calculation 
     * \param [in] nocc Number of (non-frozen) occupied orbitals.
     * \param [in] nfroz Number of frozen occupied orbitals.
     */
     void panachef_setnocc_(int_t * df_handle, int_t * nocc, int_t * nfroz)
     {
        panache_setnocc(*df_handle, *nocc, *nfroz);
     }


    /*!
     * \brief Sets the maximum number of (OpenMP) threads used
     *
     * Set to zero to use the value of the environment variable OMP_NUM_THREAD (or
     * set by omp_num_threads, or the default for this machine).
     *
     * \param [in] df_handle A handle (returned from an init function) for this DF calculation
     * \param [in] nthread Max number of threads to use
     * \param [out] actual The max number of threads that will actually be used (ie if \p nthread is zero).
     */ 
    void panachef_setnthread_(int_t * df_handle, int_t * nthread, int_t * actual)
    {
        *actual = panache_setnthread(*df_handle, *nthread);
    }



    /*!
     * \brief Prints out timing information collected so far
     *
     * All times are cumulative for all operations. The output must be set
     *  first (See Output.h)
     *
     * \param [in] df_handle A handle (returned from an init function) for this DF calculation
     */
    void panachef_printtimings_(int_t * df_handle)
    {
        panache_printtimings(*df_handle);
    }



    /*!
     * \brief Delete a tensor (from memory, disk, etc)
     *
     * \param [in] df_handle A handle (returned from an init function) for this DF calculation
     * \param [in] qflags A combination of flags specifying which tensors to delete
     */
    void panachef_delete_(int_t * df_handle, int_t * qflags)
    {
        panache_delete(*df_handle, *qflags);
    }



    /*!
     * \brief Generates various 3-index tensors
     *
     * For the \p qflags and \p storeflags parameters, see Flags.h. For example, to calculate the
     * Qmo and Qov tensors on disk,
     *
     * \code{.f90}
     * call panachef_genqtensors(df_handle, 10, 4)
     * \endcode
     *
     * Default is QSTORAGE_INMEM and not to store with QSTORAGE_BYQ
     *
     * To calculate just Qso, set \p qflags = QGEN_QSO
     *
     * \note The Qso matrix is always stored with QSTORAGE_BYQ
     * \warning Be sure to set the C-Matrix first and number of occupied orbitals first
     *          if qflags contains more than QGEN_QSO
     *
     * \note The Qso matrix is always stored with QSTORAGE_BYQ
     * \note Be sure to set the C-Matrix first!
     *
     * \param [in] df_handle A handle (returned from an init function) for the DF calculation 
     * \param [in] qflags A combination of flags specifying which tensors to generate
     * \param [in] storeflags How to store the matrix
     */
    void panachef_genqtensors_(int_t * df_handle, int_t * qflags, int_t * storeflags)
    {
        panache_genqtensors(*df_handle, *qflags, *storeflags);
    }




    /*!
     * \brief Obtain the batch size for panachef_getqbatch()
     *
     * The size will be as follows
     *
     * | Tensor | Packed Size      | Unpacked Size |
     * |--------|------------------|---------------|
     * | Qso    | nso*(nso+1)/2    |               |
     * | Qmo    | nmo*(nmo+1)/2    |               |
     * | Qoo    | nocc*(nocc+1)/2  |               |
     * | Qov    |                  | nocc*nvir     |
     * | Qvv    | nvir*(nvir+1)/2  |               |
     *
     * \param [in] df_handle A handle (returned from an init function) for the DF calculation 
     * \param [in] tensorflag Which tensor to query (see Flags.h)
     * \param [out] batchsize of batches returned by panachef_getqbatch
     */
    void panachef_qbatchsize_(int_t * df_handle, int_t * tensorflag, int_t * batchsize)
    {
        *batchsize = panache_qbatchsize(*df_handle, *tensorflag);
    }


    /*!
     * \brief Obtain the batch size for panache_getbatch()
     *
     * The size will always be naux (number of auxiliary basis functions)
     *
     * \param [in] df_handle A handle (returned from an init function) for the DF calculation 
     * \param [in] tensorflag Which tensor to query (see Flags.h)
     * \param [out] batchsize Size of batches returned by panache_getbatch()
     */
    void panachef_batchsize_(int_t * df_handle, int_t * tensorflag, int_t * batchsize)
    {
        *batchsize = panache_batchsize(*df_handle, *tensorflag);
    }


    /*!
     * \brief See if a particular tensor is stored packed
     *
     * \param [in] df_handle A handle (returned from an init function) for the DF calculation 
     * \param [in] tensorflag Which tensor to query (see Flags.h)
     * \param [out] ispacked Nonzero if the tensor is in packed storage
     */
    void panachef_ispacked_(int_t * df_handle, int_t * tensorflag, int_t * ispacked)
    {
        *ispacked = panache_ispacked(*df_handle, *tensorflag);
    }

    /*!
     * \brief Calculate a combined orbital index
     *
     * Depends on packing
     * 
     * \note This function takes into account 1-based indexing from fortran, so you don't
     *       have to (ie this function subtracts 1 from *i and *j)
     * \param [in] df_handle A handle (returned from an init function) for the DF calculation 
     * \param [in] tensorflag Which tensor to query (see Flags.h)
     * \param [in] i First orbital index
     * \param [in] j Second orbital index
     * \param [out] ij Combined orbital index, depending on packing
     */
    void panachef_calcindex_(int_t * df_handle, int_t * tensorflag, int_t * i, int_t * j, int_t * ij)
    {
        *ij = panache_calcindex(*df_handle, *tensorflag, (*i)-1, (*j)-1);
    }


    /*!
     * \brief Obtain the dimensions of a tensor
     *
     * \param [in] df_handle A handle (returned from an init function) for the DF calculation 
     * \param [in] tensorflag Which tensor to query (see Flags.h)
     * \param [out] naux Number of auxiliary basis functions
     * \param [out] ndim1 First dimension for a particular q
     * \param [out] ndim2 Second dimension for a particular q
     * \param [out] total Total tensor size (depends on packing)
     */
    void panachef_tensordimensions_(int_t * df_handle, int_t * tensorflag,
                                   int_t * naux, int_t * ndim1, int_t * ndim2, int_t * total)
    {
        *total = panache_tensordimensions(*df_handle, *tensorflag,
                                          naux, ndim1, ndim2);
    }



    /*!
     * \brief Retrieves a batch of a 3-index tensor 
     *
     * See \ref theory_page for what these tensors actually are, and memory_sec for more information
     * about memory.
     *
     * This function returns the number of batches it has stored in the buffer. The buffer
     * will contain (number of batches)*batchsize elements with
     * the index of the auxiliary basis function as the slowest index.
     *
     * The batchsize can be obtained using QBatchSize()
     *
     * Call this and process the batches, incrementing qstart by the return value,
     * until nbatch = 0
     *
     * \note Tensors are always in row-major order!
     *
     * \param [in] df_handle A handle (returned from an init function) for this DF calculation
     * \param [in] tensorflag Which tensor to get (see Flags.h)
     * \param [in] outbuf Memory location to store the tensor
     * \param [in] bufsize The size of \p outbuf (in number of doubles)
     * \param [in] qstart The starting value of q
     * \param [out] nbatch The number of batches actually stored in the buffer.
     */
    void panachef_getqbatch_(int_t  *df_handle, int_t * tensorflag,
                             double * outbuf, int_t * bufsize, int_t * qstart, int_t * nbatch)
    {
        *nbatch = panache_getqbatch(*df_handle, *tensorflag, outbuf, *bufsize, *qstart);
    }



    /*!
     * \brief Retrieves a batch of a 3-index tensor 
     *
     * See \ref theory_page for what these tensors actually are, and memory_sec for more information
     * about memory.
     *
     * This function returns the number of batches it has stored in the buffer. The buffer
     * will contain (number of batches)*naux elements with the combined orbital index
     * as the slowest index.
     *
     * The batchsize can be obtained using BatchSize()
     *
     * Call this and process the batches, incrementing qstart by the return value,
     * until this function returns zero.
     *
     * \note Tensors are always in row-major order!
     *
     * \param [in] df_handle A handle (returned from an init function) for this DF calculation
     * \param [in] tensorflag Which tensor to get (see Flags.h)
     * \param [in] outbuf Memory location to store the tensor
     * \param [in] bufsize The size of \p outbuf (in number of doubles)
     * \param [in] ijstart The starting value of q
     * \param [out] nbatch The number of batches actually stored in the buffer.
     */
    void panachef_getbatch_(int_t  *df_handle, int_t * tensorflag,
                             double * outbuf, int_t * bufsize, int_t * ijstart, int_t * nbatch)
    {
        *nbatch = panache_getbatch(*df_handle, *tensorflag, outbuf, *bufsize, *ijstart);
    }

}

