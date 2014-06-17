#ifndef PANACHE_C_INTERFACE_H
#define PANACHE_C_INTERFACE_H

#ifdef USE_64PANACHE
    #define int_t int64_t
#else
    #define int_t int32_t
#endif

extern "C" {

    struct C_ShellInfo
    {
        int_t nprim;
        int_t am;
        int_t ispure;
        double * exp; // of length nprim
        double * coef; // of length nprim
    };

    struct C_AtomCenter
    {
        const char * symbol;
        double center[3];
    };



    int_t panache_init(int_t ncenters,
                       C_AtomCenter * atoms, int_t normalized,  
                       int_t * primary_nshellspercenter, struct C_ShellInfo * primary_shells,
                       int_t * aux_nshellspercenter, struct C_ShellInfo * aux_shells,
                       const char * filename);

    int_t panache_init2(int_t ncenters,
                        C_AtomCenter * atoms, int_t normalized,
                        int_t * primary_nshellspercenter, struct C_ShellInfo * primary_shells,
                        const char * auxfilename, const char * filename);

    void panache_setcmatrix(int_t df_handle, double * cmo, int_t nmo, int_t cmo_is_trans); 
    void panache_genqso(int_t df_handle, int_t inmem);

    void panache_setoutputbuffer(int_t df_handle, double * matout, int_t matsize);
    int_t panache_getbatch_qso(int_t df_handle);
    int_t panache_getbatch_qmo(int_t dfhandle);

    void panache_cleanup(int_t df_handle);
    void panache_cleanup_all(void);

    int_t panache_qsodimensions(int_t df_handle, int_t * naux, int_t * nso2);

/*
    int_t panache_CalculateERI(int_t df_handle, double * qso, int_t qsosize, int_t shell1, int_t shell2, int_t shell3, int_t shell4, double * outbuffer, int_t buffersize);

    int_t C_CalculateERIMulti(int_t df_handle,
                            double * qso, int_t qsosize,
                            int_t shell1, int_t nshell1,
                            int_t shell2, int_t nshell2,
                            int_t shell3, int_t nshell3,
                            int_t shell4, int_t nshell4,
                            double * outbuffer, int_t buffersize);

    void C_ReorderQ_GAMESS(int_t df_handle, double * qso, int_t qsosize);
*/
}

    
#endif