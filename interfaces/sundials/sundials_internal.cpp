/*
 *    This file is part of CasADi.
 *
 *    CasADi -- A symbolic framework for dynamic optimization.
 *    Copyright (C) 2010 by Joel Andersson, Moritz Diehl, K.U.Leuven. All rights reserved.
 *
 *    CasADi is free software; you can redistribute it and/or
 *    modify it under the terms of the GNU Lesser General Public
 *    License as published by the Free Software Foundation; either
 *    version 3 of the License, or (at your option) any later version.
 *
 *    CasADi is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *    Lesser General Public License for more details.
 *
 *    You should have received a copy of the GNU Lesser General Public
 *    License along with CasADi; if not, write to the Free Software
 *    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include "sundials_internal.hpp"
#include "casadi/stl_vector_tools.hpp"
#include "casadi/matrix/matrix_tools.hpp"
#include "casadi/mx/mx_tools.hpp"
#include "casadi/sx/sx_tools.hpp"
#include "casadi/fx/mx_function.hpp"
#include "casadi/fx/sx_function.hpp"

INPUTSCHEME(IntegratorInput)
OUTPUTSCHEME(IntegratorOutput)

using namespace std;
namespace CasADi{
namespace Sundials{
  
SundialsInternal::SundialsInternal(const FX& f, const FX& g) : IntegratorInternal(f,g){
  addOption("max_num_steps",               OT_INTEGER, 10000); // maximum number of steps
  addOption("reltol",                      OT_REAL,    1e-6); // relative tolerence for the IVP solution
  addOption("abstol",                      OT_REAL,    1e-8); // absolute tolerence  for the IVP solution
  addOption("exact_jacobian",              OT_BOOLEAN,  false);
  addOption("upper_bandwidth",             OT_INTEGER); // upper band-width of banded jacobians
  addOption("lower_bandwidth",             OT_INTEGER); // lower band-width of banded jacobians
  addOption("linear_solver",               OT_STRING, "dense","","user_defined|dense|banded|iterative");
  addOption("iterative_solver",            OT_STRING, "gmres","","gmres|bcgstab|tfqmr");
  addOption("pretype",                     OT_STRING, "none","","none|left|right|both");
  addOption("max_krylov",                  OT_INTEGER,  10);        // maximum krylov subspace size
  addOption("sensitivity_method",          OT_STRING,  "simultaneous","","simultaneous|staggered");
  addOption("max_multistep_order",         OT_INTEGER, 5);
  addOption("use_preconditioner",          OT_BOOLEAN, false); // precondition an iterative solver
  addOption("stop_at_end",                 OT_BOOLEAN, false); // Stop the integrator at the end of the interval
  
  // Quadratures
  addOption("quad_err_con",                OT_BOOLEAN,false); // should the quadratures affect the step size control

  // Forward sensitivity problem
  addOption("fsens_err_con",               OT_BOOLEAN,true); // include the forward sensitivities in all error controls
  addOption("finite_difference_fsens",     OT_BOOLEAN, false); // use finite differences to approximate the forward sensitivity equations (if AD is not available)
  addOption("fsens_reltol",                OT_REAL); // relative tolerence for the forward sensitivity solution [default: equal to reltol]
  addOption("fsens_abstol",                OT_REAL); // absolute tolerence for the forward sensitivity solution [default: equal to abstol]
  addOption("fsens_scaling_factors",       OT_REALVECTOR); // scaling factor for the components if finite differences is used
  addOption("fsens_sensitiviy_parameters", OT_INTEGERVECTOR); // specifies which components will be used when estimating the sensitivity equations

  // Adjoint sensivity problem
  addOption("steps_per_checkpoint",        OT_INTEGER,20); // number of steps between two consecutive checkpoints
  addOption("interpolation_type",          OT_STRING,"hermite","type of interpolation for the adjoint sensitivities","hermite|polynomial");
  addOption("asens_upper_bandwidth",       OT_INTEGER); // upper band-width of banded jacobians
  addOption("asens_lower_bandwidth",       OT_INTEGER); // lower band-width of banded jacobians
  addOption("asens_linear_solver",         OT_STRING, "dense","","dense|banded|iterative");
  addOption("asens_iterative_solver",      OT_STRING, "gmres","","gmres|bcgstab|tfqmr");
  addOption("asens_pretype",               OT_STRING, "none","","none|left|right|both");
  addOption("asens_max_krylov",            OT_INTEGER,  10);        // maximum krylov subspace size
  addOption("asens_reltol",                OT_REAL); // relative tolerence for the adjoint sensitivity solution [default: equal to reltol]
  addOption("asens_abstol",                OT_REAL); // absolute tolerence for the adjoint sensitivity solution [default: equal to abstol]
  addOption("linear_solver_creator",       OT_LINEARSOLVER, GenericType(), "An linear solver creator function");
  addOption("linear_solver_options",       OT_DICTIONARY, GenericType(), "Options to be passed to the linear solver");
}

SundialsInternal::~SundialsInternal(){ 
}

void SundialsInternal::init(){
  // Call the base class method
  IntegratorInternal::init();
 
  // Reset checkpoints counter
  ncheck_ = 0;

  // Read options
  abstol_ = getOption("abstol");
  reltol_ = getOption("reltol");
  exact_jacobian_ = getOption("exact_jacobian");
  max_num_steps_ = getOption("max_num_steps");
  finite_difference_fsens_ = getOption("finite_difference_fsens");
  fsens_abstol_ = hasSetOption("fsens_abstol") ? double(getOption("fsens_abstol")) : abstol_;
  fsens_reltol_ = hasSetOption("fsens_reltol") ? double(getOption("fsens_reltol")) : reltol_;
  asens_abstol_ = hasSetOption("asens_abstol") ? double(getOption("asens_abstol")) : abstol_;
  asens_reltol_ = hasSetOption("asens_reltol") ? double(getOption("asens_reltol")) : reltol_;
  stop_at_end_ = getOption("stop_at_end");
  use_preconditioner_ = getOption("use_preconditioner");

  
  // Linear solver for forward integration
  if(getOption("linear_solver")=="dense"){
    linsol_f_ = SD_DENSE;
  } else if(getOption("linear_solver")=="banded") {
    linsol_f_ = SD_BANDED;
  } else if(getOption("linear_solver")=="iterative") {
    linsol_f_ = SD_ITERATIVE;

    // Iterative solver
    if(getOption("iterative_solver")=="gmres"){
      itsol_f_ = SD_GMRES;
    } else if(getOption("iterative_solver")=="bcgstab") {
      itsol_f_ = SD_BCGSTAB;
    } else if(getOption("iterative_solver")=="tfqmr") {
      itsol_f_ = SD_TFQMR;
    } else throw CasadiException("Unknown sparse solver for forward integration");
      
    // Preconditioning type
    if(getOption("pretype")=="none")               pretype_f_ = PREC_NONE;
    else if(getOption("pretype")=="left")          pretype_f_ = PREC_LEFT;
    else if(getOption("pretype")=="right")         pretype_f_ = PREC_RIGHT;
    else if(getOption("pretype")=="both")          pretype_f_ = PREC_BOTH;
    else                                           throw CasadiException("Unknown preconditioning type for forward integration");
  } else if(getOption("linear_solver")=="user_defined") {
    linsol_f_ = SD_USER_DEFINED;
  } else throw CasadiException("Unknown linear solver for forward integration");
  
  // Linear solver for backward integration
  if(getOption("asens_linear_solver")=="dense"){
    linsol_g_ = SD_DENSE;
  } else if(getOption("asens_linear_solver")=="banded") {
    linsol_g_ = SD_BANDED;
  } else if(getOption("asens_linear_solver")=="iterative") {
    linsol_g_ = SD_ITERATIVE;

    // Iterative solver
    if(getOption("asens_iterative_solver")=="gmres"){
      itsol_g_ = SD_GMRES;
    } else if(getOption("asens_iterative_solver")=="bcgstab") {
      itsol_g_ = SD_BCGSTAB;
    } else if(getOption("asens_iterative_solver")=="tfqmr") {
      itsol_g_ = SD_TFQMR;
    } else throw CasadiException("Unknown sparse solver for backward integration");
    
    // Preconditioning type
    if(getOption("asens_pretype")=="none")               pretype_g_ = PREC_NONE;
    else if(getOption("asens_pretype")=="left")          pretype_g_ = PREC_LEFT;
    else if(getOption("asens_pretype")=="right")         pretype_g_ = PREC_RIGHT;
    else if(getOption("asens_pretype")=="both")          pretype_g_ = PREC_BOTH;
    else                                           throw CasadiException("Unknown preconditioning type for backward integration");
  } else if(getOption("asens_linear_solver")=="user_defined") {
    linsol_g_ = SD_USER_DEFINED;
  } else throw CasadiException("Unknown linear solver for backward integration");
  
  // Get the linear solver creator function
  if(linsol_.isNull() && hasSetOption("linear_solver_creator")){
    linearSolverCreator linear_solver_creator = getOption("linear_solver_creator");
  
    // Allocate an NLP solver
    linsol_ = linear_solver_creator(CRSSparsity());
  
    // Pass options
    if(hasSetOption("linear_solver_options")){
      const Dictionary& linear_solver_options = getOption("linear_solver_options");
      linsol_.setOption(linear_solver_options);
    }
  }
}

void SundialsInternal::deepCopyMembers(std::map<SharedObjectNode*,SharedObject>& already_copied){
  IntegratorInternal::deepCopyMembers(already_copied);
  jac_ = deepcopy(jac_,already_copied);
  linsol_ = deepcopy(linsol_,already_copied);
}

SundialsIntegrator SundialsInternal::jac(bool with_x, bool with_p){
  // Make sure that we need sensitivities w.r.t. X0 or P (or both)
  casadi_assert(with_x || with_p);
  
  // Type cast to SXMatrix (currently supported)
  SXFunction f = shared_cast<SXFunction>(f_);
  if(f.isNull() != f_.isNull()) return SundialsIntegrator();
  
  // Number of state derivatives
  int n_xdot = f_.input(DAE_XDOT).numel();
  
  // Number of sensitivities
  int ns_x = with_x*nx_;
  int ns_p = with_p*np_;
  int ns = ns_x + ns_p;

  // Sensitivities and derivatives of sensitivities
  vector<SXMatrix> x_sens = ssym("x_sens",f.input(DAE_X).sparsity(),ns);
  vector<SXMatrix> z_sens = ssym("z_sens",f.input(DAE_Z).sparsity(),ns);
  vector<SXMatrix> xdot_sens = ssym("xdot_sens",f.input(DAE_XDOT).sparsity(),ns);
  
  // Directional derivative seeds
  vector<vector<SXMatrix> > fseed(ns);
  for(int d=0; d<ns; ++d){
    fseed[d].resize(DAE_NUM_IN);
    fseed[d][DAE_X] = x_sens[d];
    fseed[d][DAE_Z] = z_sens[d];
    fseed[d][DAE_P] = SXMatrix(f.input(DAE_P).sparsity());
    if(with_p && d>=ns_x){
      fseed[d][DAE_P](d-ns_x) = 1;
    }
    fseed[d][DAE_T] = SXMatrix(f.input(DAE_T).sparsity());
    if(n_xdot>0){
      fseed[d][DAE_XDOT] = xdot_sens[d];
    } else {
      fseed[d][DAE_XDOT] = SXMatrix(f.input(DAE_XDOT).sparsity());
    }
  }
  
  // Calculate directional derivatives
  vector<vector<SXMatrix> > fsens(ns,f.outputsSX());
  vector<vector<SXMatrix> > aseedsens;
  f.evalSX(f.inputsSX(),const_cast<vector<SXMatrix>&>(f.outputsSX()),fseed,fsens,aseedsens,aseedsens,true);
  
  // Sensitivity equation (ODE)
  SXMatrix ode_aug = f.outputSX(DAE_ODE);
  for(int d=0; d<ns; ++d){
    ode_aug.append(fsens[d][DAE_ODE]);
  }

  // Sensitivity equation (ALG)
  SXMatrix alg_aug = f.outputSX(DAE_ALG);
  for(int d=0; d<ns; ++d){
    alg_aug.append(fsens[d][DAE_ALG]);
  }
  
  // Sensitivity equation (QUAD)
  SXMatrix quad_aug = f.outputSX(DAE_QUAD);
  for(int d=0; d<ns; ++d){
    quad_aug.append(fsens[d][DAE_QUAD]);
  }
  
  // Input arguments for the augmented DAE
  vector<SXMatrix> faug_in(DAE_NUM_IN);
  faug_in[DAE_T] = f.inputSX(DAE_T);
  faug_in[DAE_X] = vertcat(f.inputSX(DAE_X),vertcat(x_sens));
  if(nz_>0) faug_in[DAE_Z] = vertcat(f.inputSX(DAE_Z),vertcat(z_sens));
  if(n_xdot>0) faug_in[DAE_XDOT] = vertcat(f.inputSX(DAE_XDOT),vertcat(xdot_sens));
  faug_in[DAE_P] = f.inputSX(DAE_P);
  
  // Create augmented DAE function
  SXFunction ffcn_aug(faug_in,daeOut("ode",ode_aug, "alg",alg_aug, "quad",quad_aug));

  // Create integrator instance
  SundialsIntegrator integrator;
  integrator.assignNode(create(ffcn_aug,FX()));

  // Set options
  integrator.setOption(dictionary());
    
  return integrator;
}

CRSSparsity SundialsInternal::getJacSparsity(int iind, int oind){
  // Default (dense) sparsity
    return FXInternal::getJacSparsity(iind,oind);
}

FX SundialsInternal::jacobian(const std::vector<std::pair<int,int> >& jblocks){
  bool with_x = false, with_p = false;
  for(int i=0; i<jblocks.size(); ++i){
    if(jblocks[i].second == INTEGRATOR_P){
      casadi_assert_message(jblocks[i].first == INTEGRATOR_XF,"IntegratorInternal::jacobian: Not derivative of state"); // can be removed?
      with_p = true;
    } else if(jblocks[i].second == INTEGRATOR_X0){
      casadi_assert_message(jblocks[i].first == INTEGRATOR_XF,"IntegratorInternal::jacobian: Not derivative of state"); // can be removed?
      with_x = true;
    }
  }
  
  // Create a new integrator for the forward sensitivity equations
  SundialsIntegrator fwdint = jac(with_x,with_p);
  
  // If creation was successful
  if(!fwdint.isNull()){
    fwdint.init();

    // Number of sensitivities
    int ns_x = with_x*nx_;
    int ns_p = with_p*np_;
    int ns = ns_x + ns_p;
    
    // Symbolic input of the Jacobian
    vector<MX> jac_in = symbolicInput();
    
    // Input to the augmented integrator
    vector<MX> fwdint_in(INTEGRATOR_NUM_IN);
    
    // Pass parameters without change
    fwdint_in[INTEGRATOR_P] = jac_in[INTEGRATOR_P];
    
    // Get the state
    MX x0 = jac_in[INTEGRATOR_X0];
    
    // Initial condition for the sensitivitiy equations
    DMatrix x0_sens(ns*nx_,1,0);
    
    if(with_x){
      for(int i=0; i<nx_; ++i){
	x0_sens.data()[i + i*ns_x] = 1;
      }
    }

    // Finally, we are ready to pass the initial condition for the state and state derivative
    fwdint_in[INTEGRATOR_X0] = vertcat(x0,MX(x0_sens));
    
    // Call the integrator with the constructed input (in fact, create a call node)
    vector<MX> fwdint_out = fwdint.call(fwdint_in);
    MX xf_aug = fwdint_out[INTEGRATOR_XF];
    MX qf_aug = fwdint_out[INTEGRATOR_QF];
    
    // Get the state and quadrature at the final time
    MX xf = xf_aug[range(nx_)];
    MX qf = qf_aug[range(nq_)];
    
    // Get the sensitivitiy equations state at the final time
    MX xf_sens = xf_aug[range(nx_,(ns+1)*nx_)];
    MX qf_sens = qf_aug[range(nq_,(ns+1)*nq_)];

    // Reshape the sensitivity state and state derivatives
    xf_sens = trans(reshape(xf_sens,ns,nx_));
    qf_sens = trans(reshape(qf_sens,ns,nq_));
    
    // Split up the Jacobians in parts for x0 and p
    MX J_xf_x0 = xf_sens(range(xf_sens.size1()),range(ns_x));
    MX J_xf_p  = xf_sens(range(xf_sens.size1()),range(ns_x,ns));
    MX J_qf_x0 = qf_sens(range(qf_sens.size1()),range(ns_x));
    MX J_qf_p  = qf_sens(range(qf_sens.size1()),range(ns_x,ns));
    
    // Output of the Jacobian
    vector<MX> jac_out(jblocks.size());
    for(int i=0; i<jblocks.size(); ++i){
      bool is_jac = jblocks[i].second >=0;
      bool is_x0 = jblocks[i].second==INTEGRATOR_X0;
      bool is_xf = jblocks[i].first==INTEGRATOR_XF;
      if(is_jac){
        if(is_x0){
          jac_out[i] = is_xf ? J_xf_x0 : J_qf_x0;
        } else {
          jac_out[i] = is_xf ? J_xf_p : J_qf_p;
        }
      } else {
        jac_out[i] = is_xf ? xf : qf;
      }
    }
    MXFunction intjac(jac_in,jac_out);

    return intjac;
  } else {
    return FXInternal::jacobian(jblocks);
  }
}

void SundialsInternal::setInitialTime(double t0){
  t0_ = t0;
}

void SundialsInternal::setFinalTime(double tf){
  tf_ = tf;
}

void SundialsInternal::reset(int nsens, int nsensB, int nsensB_store){
  // Reset the base classes
  IntegratorInternal::reset(nsens,nsensB,nsensB_store);
  
  // Go to the start time
  t_ = t0_;
}


} // namespace Sundials
} // namespace CasADi


