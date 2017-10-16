#include "getfem/getfem_models.h"
#include "getfem/getfem_assembling.h"
#include "getfem/getfem_model_solvers.h"
#include "getfem/getfem_export.h"
#include "getfem/getfem_regular_meshes.h"
#include "getfem/getfem_mesher.h"
#include "gmm/gmm.h"
#include "getfem/getfem_generic_assembly.h"
#include "getfem/getfem_mesh_fem.h"
#include "AMG_Interface.h"
/**************************************************************************/
/*  main program.                                                         */
/**************************************************************************/

/* some GetFEM++ types that we will be using */
using bgeot::base_small_vector; /* special class for small (dim<16) vectors */
using bgeot::base_node; /* geometrical nodes (derived from base_small_vector)*/
using bgeot::scalar_type; /* = double */
using bgeot::size_type;   /* = unsigned long */


typedef gmm::rsvector<scalar_type> sparse_vector_type;
typedef gmm::row_matrix<sparse_vector_type> sparse_matrix_type;
typedef gmm::col_matrix<sparse_vector_type> col_sparse_matrix_type;
typedef std::vector<scalar_type> plain_vector;

enum { DIRICHLET_BOUNDARY_NUM = 0, NEUMANN_BOUNDARY_NUM = 1, INNER_FACES = 2};
enum { DIRICHLET_WITH_MULTIPLIERS = 0, DIRICHLET_WITH_PENALIZATION = 1};
enum { BOTTOM = 2, TOP = 1 , LEFT = 3, RIGHT =4};
getfem::base_vector assembly(getfem::base_vector U,
		getfem::base_vector P,
		getfem::base_vector U_old,
		getfem::base_vector P_old, 
		getfem::mesh_im mim,      /* the integration methods. */
		getfem::mesh_fem mf_u,    /* the main mesh_fem, for the Laplacian solution */
		getfem::mesh_fem mf_p     /* mesh_fem for the pressure                    */

		);
// Exact solution. Allows an interpolation for the Dirichlet condition.
scalar_type sol_u(const base_node &x) { return 0.; }
// Right hand side. Allows an interpolation for the source term.
scalar_type sol_f(const base_node &x) { return 1.; }
// Gradient of the solution. Allows an interpolation for the Neumann term.
base_small_vector sol_grad(const base_node &x)
{ return base_small_vector(0.*cos(x[0]+x[1]), 0.*cos(x[0]+x[1])); }
// vectorial velocity field
base_small_vector vel_vec(const base_node &x)
{ return base_small_vector(1*x[1], 0.2*(x[0])); }
/////////////////////////////////////////////////////
//////////////////////////////////////////////////
////////////////// MAIN //////////////////////////
/////////////////////////////////////////////////////
//////////////////////////////////////////////////

int main(int argc, char *argv[]) {

	GMM_SET_EXCEPTION_DEBUG; // Exceptions make a memory fault, to debug.
	FE_ENABLE_EXCEPT;        // Enable floating point exception for Nan.


	std::cout<< "here we try a generic assembly"<<std::endl; 
	bool complex_version=false;
	//////////////MESH

	std::string MESH_TYPE ="GT_LINEAR_QK(2)" ;
	bgeot::pgeometric_trans pgt = 
		bgeot::geometric_trans_descriptor(MESH_TYPE);

	size_type N;
	N = pgt->dim();
	std::vector<size_type> nsubdiv(N);
	int NX=8;
	std::fill(nsubdiv.begin(),nsubdiv.end(),NX);
	getfem::mesh mesh;        /* the mesh */

// getfem::pmesher_signed_distance
// mo = getfem::new_mesher_rectangle(base_node(0., 0.), base_node(2., 2.));

getfem::regular_unit_mesh(mesh, nsubdiv, pgt, 0);

  bgeot::base_matrix M(N,N);
  for (size_type i=0; i < N; ++i) {
    static const char *t[] = {"LX","LY","LZ"};
    M(i,i) = (i<3) ? 2.0 : 1.0;
  }
  // if (N>1) { M(0,1) = PARAM.real_value("INCLINE") * PARAM.real_value("LY"); }

  /* scale the unit mesh to [LX,LY,..] and incline it */
  mesh.transformation(M);




// std::vector<getfem::base_node> fixed;
// double h = 0.1;        // Approximate mesh size
// getfem::build_mesh(mesh, mo, h, fixed, 2, -2);

	//  bgeot::base_matrix M(N,N);
	//  for (size_type i=0; i < N; ++i) {
	//    M(i,i) = 1.0;
	//  }
	//  if (N>1) { M(0,1) = 0; }
	//
	//  mesh.transformation(M);

	//

	/* set the finite element on the mf_u */
	std::string FEM_TYPE  = "FEM_QK(2,2)";
	std::string INTEGRATION ="IM_GAUSS_PARALLELEPIPED(2,6)";
	std::string FEM_TYPE_P  = "FEM_QK(2,1)";
	//================== principal ========================================
	getfem::mesh_im mim(mesh);      /* the integration methods. */
	getfem::mesh_fem mf_u(mesh);    /* the main mesh_fem, for the Laplacian solution */
	getfem::mesh_fem mf_p(mesh);     /* mesh_fem for the pressure                    */
	getfem::mesh_fem mf_rhs(mesh);  /* the mesh_fem for the right hand side(f(x),..) */
	/* set the finite element on the mf_u */
	// mim(mesh); mf_u(mesh); mf_rhs(mesh);
	getfem::pfem pf_u = getfem::fem_descriptor(FEM_TYPE);
	getfem::pfem pf_p = getfem::fem_descriptor(FEM_TYPE_P);
	getfem::pintegration_method ppi = getfem::int_method_descriptor(INTEGRATION);

	mim.set_integration_method(mesh.convex_index(), ppi);
	mf_u.set_finite_element(mesh.convex_index(), pf_u);
	mf_u.set_qdim(bgeot::dim_type(2)); //number of variable
	mf_p.set_finite_element(mesh.convex_index(), pf_p);

	GMM_ASSERT1(pf_u->is_lagrange(), "You are using a non-lagrange FEM. "
			<< "In that case you need to set "
			<< "DATA_FEM_TYPE in the .param file");
	mf_rhs.set_finite_element(mesh.convex_index(), pf_u);

	/* set boundary conditions
	 * (Neuman on the upper face, Dirichlet elsewhere) */
	std::cout << "Selecting Neumann and Dirichlet boundaries\n";
	getfem::mesh_region border_faces;
	getfem::outer_faces_of_mesh(mesh, border_faces);
	for (getfem::mr_visitor i(border_faces); !i.finished(); ++i) {
		assert(i.is_face());
		base_node un = mesh.normal_of_face_of_convex(i.cv(), i.f());
		un /= gmm::vect_norm2(un);
		//if (gmm::abs(un[N-1] - 1.0) < 1.0E-7) { // new Neumann face
		//	mesh.region(NEUMANN_BOUNDARY_NUM).add(i.cv(), i.f());
		//} else {
		//	mesh.region(DIRICHLET_BOUNDARY_NUM).add(i.cv(), i.f());
		//}
		if (gmm::abs(un[N-1] - 1.0) < 1.0E-7) { // new Neumann face
			mesh.region(TOP).add(i.cv(), i.f());
		} else if (gmm::abs(un[N-1] + 1.0) < 1.0E-7) {
			mesh.region(BOTTOM).add(i.cv(), i.f());
		} else if (gmm::abs(un[N-2] + 1.0) < 1.0E-7) {
			mesh.region(LEFT).add(i.cv(), i.f());
		} else if (gmm::abs(un[N-2] - 1.0) < 1.0E-7) {
			mesh.region(RIGHT).add(i.cv(), i.f());
		}
		else {
			mesh.region(DIRICHLET_BOUNDARY_NUM).add(i.cv(), i.f());
		}
	}




	// 


	//Boudanry conditions
	getfem::size_type nbdofu  = mf_u.nb_dof();
	getfem::size_type nbdofp = mf_p.nb_dof();
	getfem::base_vector U(nbdofu);
	getfem::base_vector P(nbdofp);
	getfem::base_vector U_old(nbdofu);
	getfem::base_vector P_old(nbdofp);
	getfem::base_vector TU(nbdofu+nbdofp);


	std::string datafilename="biot.";
        int printstep=1; 
	for(int istep = 1; istep< 10 ; istep++){
		TU=assembly(U, P, U_old, P_old,mim,  mf_u, mf_p);
		gmm::copy(gmm::sub_vector(TU,gmm::sub_interval(0,nbdofu)),U);	
		gmm::copy(gmm::sub_vector(TU,gmm::sub_interval(nbdofu, nbdofp)),P);

		if (istep%printstep==0){
			getfem::vtk_export exp1(datafilename + std::to_string(istep) + ".vtk",   1);

		// exp1.set_header("TIME 1 1 double \n" + std::to_string(istep*0.1));
			//       exp.exporting(mf_u);     exp.write_point_data(mf_u, U, "u");
			exp1.exporting(mf_u);  	exp1.write_point_data(mf_u, U, "u");  exp1.write_point_data(mf_p, P, "p");
		}
	}
	return 0; 
}





getfem::base_vector assembly(
		getfem::base_vector U,
		getfem::base_vector P,
		getfem::base_vector U_old,
		getfem::base_vector P_old, 
		getfem::mesh_im mim,      /* the integration methods. */
		getfem::mesh_fem mf_u,    /* the main mesh_fem, for the Laplacian solution */
		getfem::mesh_fem mf_p     /* mesh_fem for the pressure                    */

		){
	// getfem::model laplacian_model;
	getfem::ga_workspace workspace;
	getfem::size_type nbdofu  = mf_u.nb_dof();
	getfem::size_type nbdofp = mf_p.nb_dof();


	std::cout<<"ndofu "<<nbdofu<<" ndofup"<<nbdofp<<std::endl;
	gmm::copy(U,U_old);gmm::copy(P,P_old);	
	gmm::clean(P_old, 1E-10);gmm::clean(U_old, 1E-10);
	gmm::clean(P, 1E-10);gmm::clean(U, 1E-10);
	double mu=1.5e+6;
	double dt=1.e+1;
	getfem::base_vector invdt(1); invdt[0] = 1/dt;
	workspace.add_fixed_size_constant("invdt", invdt);
//---------------------------------------------------------
	getfem::base_vector vmu(1); vmu[0] = mu;
	workspace.add_fixed_size_constant("mu", vmu);
//---------------------------------------------------------
	double poisson =0.2;
	getfem::base_vector lambda(1); lambda[0] = (2 * mu * poisson)/(1 - 2 * poisson) ;
	workspace.add_fixed_size_constant("lambda", lambda);
//---------------------------------------------------------
	getfem::base_vector alpha(1); alpha[0] = 0.5;
	workspace.add_fixed_size_constant("alpha", alpha);
//---------------------------------------------------------
	getfem::base_vector permeability(1); permeability[0] = 1.e-3;
	workspace.add_fixed_size_constant("permeability", permeability);
//---------------------------------------------------------
	getfem::base_vector force(1); force[0] = 1.e+6;
	workspace.add_fixed_size_constant("force", force);
//---------------------------------------------------------
	workspace.add_fem_variable("u", mf_u, gmm::sub_interval(0, nbdofu), U);
	workspace.add_fem_variable("p", mf_p, gmm::sub_interval(nbdofu, nbdofp), P);
	workspace.add_fem_variable("u_old", mf_u, gmm::sub_interval(0, nbdofu), U_old);
	workspace.add_fem_variable("p_old", mf_p, gmm::sub_interval(nbdofu,nbdofp), P_old);
	workspace.add_expression("2*mu*Sym(Grad_u):Grad_Test_u"
			"+ alpha*p*Trace(Grad_Test_u) + invdt*alpha*Test_p*Trace(Sym(Grad_u))"
			"+invdt*p.Test_p + permeability*Grad_p.Grad_Test_p + lambda*Div_u*Div_Test_u"
			, mim);
	getfem::model_real_sparse_matrix K(nbdofu+nbdofp, nbdofu+nbdofp);
	workspace.set_assembled_matrix(K);
	workspace.assembly(2);
	workspace.clear_expressions();

	//======= RHS =====================
	workspace.add_expression("[0,-0].Test_u +[+0.0].Test_p + invdt*p_old.Test_p + invdt*alpha*Test_p*Trace(Sym(Grad_u_old))", mim);
	getfem::base_vector L(nbdofu+nbdofp);
	workspace.set_assembled_vector(L);
	workspace.assembly(1);
	workspace.clear_expressions();

	//Boudanry conditions
	getfem::base_vector penalty(1); penalty[0] = 1.e+8;
	workspace.add_fixed_size_constant("penalty", penalty);
	//Matrix term
	//workspace.add_expression("0*penalty*u.Test_u" "+ 0*penalty*p*Test_p", mim, TOP); // 1 is the region
	// workspace.add_expression("penalty*u.Test_u" "+ 0*penalty*p*Test_p", mim, BOTTOM); // 1 is the region
	//workspace.add_expression("0*penalty*u.Test_u" "+ 0*penalty*p*Test_p", mim, BOTTOM); // 1 is the region	
	workspace.add_expression("penalty*p*Test_p", mim, LEFT); workspace.add_expression("penalty*p*Test_p", mim, RIGHT);// 1 is the region	
	workspace.assembly(2);
	workspace.clear_expressions();
	//rhs term
	// workspace.add_expression("0*penalty*u.Test_u" "+ penalty*0*p*Test_p", mim, TOP); // 1 is the region
	workspace.add_expression("[0,-2*force].Test_u" , mim, TOP); //neumann disp
	// workspace.add_expression("0*penalty*u.Test_u" "+ 0*penalty*0*p*Test_p", mim, BOTTOM); // 1 is the region
	workspace.add_expression("[0,+2*force].Test_u" , mim, BOTTOM); //neumann disp
	workspace.add_expression("0*penalty*p*Test_p", mim, LEFT); workspace.add_expression("0*penalty*p*Test_p", mim, RIGHT);
	workspace.assembly(1);
	workspace.clear_expressions();
	// ==============================================================
	getfem::base_vector TU(nbdofu+nbdofp);
	gmm::copy(U, gmm::sub_vector(TU,gmm::sub_interval(0, nbdofu)));				
	gmm::copy(P, gmm::sub_vector(TU,gmm::sub_interval(nbdofu,nbdofp)));	
	//===================================================================
	size_type restart = 50;
	gmm::identity_matrix PM; // no precond
	gmm::iteration iter(1.e-12);  // iteration object with the max residu
	iter.set_noisy(1);               // output of iterations (2: sub-iteration)
	iter.set_maxiter(2000); // maximum number of iterations
	//gmm::gmres(K, TU, L, PM, restart, iter);

	



//solution with CG
//	gmm::identity_matrix PS;  // optional scalar product
//	gmm::cg(K, TU, L, PS, PM, iter);
//end solution with CG



	getfem::mesh mymesh = mf_u.linked_mesh ();        /* the mesh */
int * dofpt; dofpt=new int[nbdofu+nbdofp];for (int ia=0; ia< nbdofu+nbdofp ; ia++) dofpt[ia]=-1;
int pt_counter=0;int lnpt_counter=0;
    //// List all the convexes
    dal::bit_vector nn = mymesh.convex_index();
    bgeot::size_type i;
    for (i << nn; i != bgeot::size_type(-1); i << nn) {
	int ndof_el=mf_u.nb_basic_dof_of_element(i) ;
     // std::cout << "Convex of index " << i <<" number of u dof "<<ndof_el << std::endl;
 
     getfem::mesh_fem::ind_dof_ct dof;
     dof = mf_u.ind_basic_dof_of_element(i);
     for (int idof=0; idof< ndof_el ; idof+=mf_u.get_qdim())  {
		 if(dofpt[dof[idof]]==-1){
			 for (int idim=0; idim< mf_u.get_qdim(); idim++) dofpt[dof[idof+idim]]=pt_counter;
			 if(idof==0 * mf_u.get_qdim() || idof==2 * mf_u.get_qdim()|| idof==6 * mf_u.get_qdim() || idof==8 * mf_u.get_qdim()){
			 dofpt[nbdofu+lnpt_counter]=pt_counter;
			 lnpt_counter++;
		 }			 
			 pt_counter++;
			 }

		 }
       
    }


////////////////////////////////////AMG INTERFACE
std::cout<<"converting A"<<std::endl;
gmm::csr_matrix<scalar_type> A_csr;
gmm::clean(K, 1E-12);
gmm::copy(K, A_csr);
std::cout<<"converting X"<<std::endl;
std::vector<scalar_type> X,  B;
gmm::resize(X,nbdofu+nbdofp); gmm::clean(X, 1E-12);
gmm::copy(TU,X);
std::cout<<"converting B"<<std::endl;
gmm::resize(B,nbdofu+nbdofp);gmm::clean(B, 1E-12);
gmm::copy(L,B);

AMG amg("Biot");
amg.set_pt2uk(dofpt , nbdofu, nbdofp, pt_counter);
amg.solve(A_csr, X , B);
gmm::copy(amg.getsol(),TU);
//////////////////////////////////////////////



	gmm::copy(gmm::sub_vector(TU,gmm::sub_interval(0,nbdofu)),U);	
	gmm::copy(gmm::sub_vector(TU,gmm::sub_interval(nbdofu, nbdofp)),P);	
	return(TU);
}







