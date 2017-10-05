// AMG_Interrface.h
#ifndef aamg
#define aamg


#include<iostream> 

#include "getfem/getfem_mesher.h"
#include "gmm/gmm.h"

#include "samg.h"
/* default 4 Byte integer types */
#ifndef APPL_INT
#define APPL_INT int
#endif

using bgeot::scalar_type; 

class AMG {
  private:
std::vector<scalar_type> sol_vec;


  public:
  AMG(gmm::csr_matrix<scalar_type> A_csr, std::vector<scalar_type> U, std::vector<scalar_type> B );
  std::vector<scalar_type> getsol(){return sol_vec;}
};

#endif
