/*
 *    This file is part of CasADi.
 *
 *    CasADi -- A symbolic framework for dynamic optimization.
 *    Copyright (C) 2010-2014 Joel Andersson, Joris Gillis, Moritz Diehl,
 *                            K.U. Leuven. All rights reserved.
 *    Copyright (C) 2011-2014 Greg Horn
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


#ifndef CASADI_LAPACK_QR_HPP
#define CASADI_LAPACK_QR_HPP

#include "casadi/core/function/linsol_internal.hpp"
#include <casadi/interfaces/lapack/casadi_linsol_lapackqr_export.h>

/** \defgroup plugin_Linsol_lapackqr
*
* This class solves the linear system <tt>A.x=b</tt> by making an QR factorization of A: \n
* <tt>A = Q.R</tt>, with Q orthogonal and R upper triangular
*/

/** \pluginsection{Linsol,lapackqr} */

/// \cond INTERNAL
namespace casadi {
  struct CASADI_LINSOL_LAPACKQR_EXPORT LapackQrMemory : public LinsolMemory {
    // Matrix
    std::vector<double> mat;

    // The scalar factors of the elementary reflectors
    std::vector<double> tau;

    // qr work array
    std::vector<double> work;
  };

  /** \brief  \pluginbrief{Linsol,lapackqr}
   *
   @copydoc Linsol_doc
   @copydoc plugin_Linsol_lapackqr
   *
   */
  class CASADI_LINSOL_LAPACKQR_EXPORT LapackQr : public LinsolInternal {
  public:
    // Create a linear solver given a sparsity pattern and a number of right hand sides
    LapackQr(const std::string& name);

    /** \brief  Create a new Linsol */
    static LinsolInternal* creator(const std::string& name) {
      return new LapackQr(name);
    }

    // Destructor
    virtual ~LapackQr();

    // Initialize the solver
    virtual void init(const Dict& opts);

    ///@{
    /** \brief Options */
    static Options options_;
    virtual const Options& get_options() const { return options_;}
    ///@}

    /** \brief Create memory block */
    virtual void* alloc_memory() const { return new LapackQrMemory();}

    /** \brief Free memory block */
    virtual void free_memory(void *mem) const { delete static_cast<LapackQrMemory*>(mem);}

    /** \brief Initalize memory block */
    virtual void init_memory(void* mem) const;

    // Set sparsity pattern
    virtual void reset(void* mem, const int* sp) const;

    // Factorize the linear system
    virtual void factorize(void* mem, const double* A) const;

    // Solve the linear system
    virtual void solve(void* mem, double* x, int nrhs, bool tr) const;

    /// A documentation string
    static const std::string meta_doc;

    // Get name of the plugin
    virtual const char* plugin_name() const { return "lapackqr";}

    // Maximum number of right-hand-sides
    int max_nrhs_;

    virtual size_t sz_w() const { return LinsolInternal::sz_w()+100;}

    void generate(CodeGenerator& g, const std::string& mem,
      const std::vector<int>& arg, const std::vector<int>& res,
      const Sparsity& A,
      int nrhs, bool transpose) const override;

    bool can_generate() const override { return true; }

  };

} // namespace casadi

/// \endcond
#endif // CASADI_LAPACK_QR_HPP
