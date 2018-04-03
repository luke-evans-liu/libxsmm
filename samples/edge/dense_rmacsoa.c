/******************************************************************************
** Copyright (c) 2018, Intel Corporation                                     **
** All rights reserved.                                                      **
**                                                                           **
** Redistribution and use in source and binary forms, with or without        **
** modification, are permitted provided that the following conditions        **
** are met:                                                                  **
** 1. Redistributions of source code must retain the above copyright         **
**    notice, this list of conditions and the following disclaimer.          **
** 2. Redistributions in binary form must reproduce the above copyright      **
**    notice, this list of conditions and the following disclaimer in the    **
**    documentation and/or other materials provided with the distribution.   **
** 3. Neither the name of the copyright holder nor the names of its          **
**    contributors may be used to endorse or promote products derived        **
**    from this software without specific prior written permission.          **
**                                                                           **
** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS       **
** "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT         **
** LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR     **
** A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT      **
** HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,    **
** SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED  **
** TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR    **
** PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF    **
** LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING      **
** NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS        **
** SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.              **
******************************************************************************/
/* Alexander Heinecke (Intel Corp.)
******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include <sys/time.h>

#include <libxsmm.h>

#if defined(__EDGE_EXECUTE_F32__)
#define REALTYPE float
#else
#define REALTYPE double
#endif


static double sec(struct timeval start, struct timeval end) {
  return ((double)(((end.tv_sec * 1000000 + end.tv_usec) - (start.tv_sec * 1000000 + start.tv_usec)))) / 1.0e6;
}

void matMulFusedAC(       unsigned short  i_r,
                          unsigned int    i_m,
                          unsigned int    i_n,
                          unsigned int    i_k,
                          unsigned int    i_ldA,
                          unsigned int    i_ldB,
                          unsigned int    i_ldC,
                          REALTYPE           i_beta,
                    const REALTYPE           *i_a,
                    const REALTYPE           *i_b,
                          REALTYPE           *o_c ) {
  unsigned int l_m = 0;
  unsigned int l_n = 0;
  unsigned int l_r = 0;
  unsigned int l_k = 0;

  // init result matrix
  for( l_m = 0; l_m < i_m; l_m++ ) {
    for( l_n = 0; l_n < i_n; l_n++ ) {
      for( l_r = 0; l_r < i_r; l_r++ ) {
        o_c[l_m*i_ldC*i_r + l_n*i_r + l_r] = (i_beta != (REALTYPE)0) ? o_c[l_m*i_ldC*i_r + l_n*i_r + l_r] * i_beta : 0;
      }
    }
  }

  for( l_k = 0; l_k < i_k; l_k++ ) {
    for( l_m = 0; l_m < i_m; l_m++ ) {
      for( l_n = 0; l_n < i_n; l_n++ ) {
        for( l_r = 0; l_r < i_r; l_r++ ) {
          o_c[l_m*i_ldC*i_r + l_n*i_r + l_r] += i_a[l_m*i_ldA*i_r + l_k*i_r + l_r] * i_b[l_k*i_ldB + l_n];
        }
      }
    }
  }
}

int main(int agrc, char* argv[]) {
#if defined(__EDGE_EXECUTE_F32__)
  unsigned int l_r = 16;
#else
  unsigned int l_r = 8;
#endif
  unsigned int l_m = atoi(argv[1]);
  unsigned int l_n = atoi(argv[2]);
  unsigned int l_k = atoi(argv[3]);
  REALTYPE l_beta = atof(argv[4]);
  REALTYPE l_alpha = 1.0;
  unsigned int l_reps = atoi(argv[5]);
  double flops = (double)l_m * (double)l_n * (double)l_k * (double)l_r * (double)l_reps;

  REALTYPE* a = (REALTYPE*)  _mm_malloc( l_m*l_k*l_r*sizeof(REALTYPE), 64 );
  REALTYPE* b = (REALTYPE*)  _mm_malloc( l_k*l_n*sizeof(REALTYPE), 64 );
  REALTYPE* c1 = (REALTYPE*) _mm_malloc( l_m*l_n*l_r*sizeof(REALTYPE), 64 );
  REALTYPE* c2 = (REALTYPE*) _mm_malloc( l_m*l_n*l_r*sizeof(REALTYPE), 64 );

  libxsmm_descriptor_blob l_xgemm_blob;
  const libxsmm_gemm_descriptor* l_xgemm_desc = 0;
  LIBXSMM_MMFUNCTION_TYPE(REALTYPE) mykernel = NULL;
  const libxsmm_gemm_prefetch_type prefetch = LIBXSMM_GEMM_PREFETCH_NONE;
  const int flags = LIBXSMM_GEMM_FLAGS('N', 'N');

  struct timeval l_start, l_end;
  double l_total_ref, l_total_opt;
  double max_error = 0.0;
  double gflops_ref = 0.0;
  double gflops_opt = 0.0;
  unsigned int i = 0;

  for ( i = 0; i < l_m*l_n*l_r; ++i ) {
    c1[i] = (REALTYPE)drand48();
  }
  for ( i = 0; i < l_m*l_n*l_r; ++i ) {
    c2[i] = c1[i];
  }
  for ( i = 0; i < l_m*l_k*l_r; ++i ) {
    a[i] = (REALTYPE)drand48();
  }
  for ( i = 0; i < l_k*l_n; ++i ) {
    b[i] = (REALTYPE)drand48();
  }

  /* JIT code */
  l_xgemm_desc = libxsmm_gemm_descriptor_dinit(&l_xgemm_blob, LIBXSMM_GEMM_PRECISION(REALTYPE),
    l_m, l_n, l_k, l_k, l_n, l_n, l_alpha, l_beta, flags, prefetch);
#if defined(__EDGE_EXECUTE_F32__)
  mykernel = libxsmm_create_rm_ac_soa( l_xgemm_desc ).smm;
#else
  mykernel = libxsmm_create_rm_ac_soa( l_xgemm_desc ).dmm;
#endif

  /* run reference */
  matMulFusedAC( l_r,
                  l_m, l_n, l_k,
                  l_k,
                  l_n,
                  l_n,
                  l_beta,
                  a,
                  b,
                  c1);

  /* run optimized */
  mykernel( a, b, c2 );

  /* check correctnes */ 
  for ( i = 0; i < l_m*l_n*l_r; ++i ) {
    if ( max_error < fabs( c1[i] - c2[i] ) ) {
      max_error = fabs( c1[i] - c2[i] );
    }
  } 

  printf("Max. Error: %f\n", max_error);

  /* lets run some perfoermance test */
  gettimeofday(&l_start, NULL);
  for ( i = 0; i < l_reps; ++i ) {
    /* run reference */
    matMulFusedAC( l_r,
                    l_m, l_n, l_k,
                    l_k,
                    l_n,
                    l_n,
                    l_beta,
                    a,
                    b,
                    c1);
  }
  gettimeofday(&l_end, NULL);
  l_total_ref = sec(l_start, l_end);

  gettimeofday(&l_start, NULL);
  for ( i = 0; i < l_reps; ++i ) {
    /* run optimized */
    mykernel( a, b, c2);
  }
  gettimeofday(&l_end, NULL);
  l_total_opt = sec(l_start, l_end);

  gflops_ref = (flops/l_total_ref)/1e9;
  gflops_opt = (flops/l_total_opt)/1e9;

  printf("GFLOPS ref: %f\n", gflops_ref);
  printf("GFLOPS opt: %f\n", gflops_opt);

  _mm_free( a );
  _mm_free( b );
  _mm_free( c1 );
  _mm_free( c2 );
}
