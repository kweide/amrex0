

#include <cstring>
#include <cstdlib>

#include <AMReX_BaseFab.H>
#include <AMReX_BArena.H>
#include <AMReX_CArena.H>

#if !defined(BL_NO_FORT)
#include <AMReX_BaseFab_f.H>
#endif

#ifdef BL_MEM_PROFILING
#include <AMReX_MemProfiler.H>
#endif

namespace amrex {

long private_total_bytes_allocated_in_fabs     = 0L;
long private_total_bytes_allocated_in_fabs_hwm = 0L;
long private_total_cells_allocated_in_fabs     = 0L;
long private_total_cells_allocated_in_fabs_hwm = 0L;

int BF_init::m_cnt = 0;

namespace
{
    Arena* the_arena = 0;
#ifdef CUDA
    Arena* the_nvar_arena = 0;
#endif
}

BF_init::BF_init ()
{
    if (m_cnt++ == 0)
    {
        BL_ASSERT(the_arena == 0);

#if defined(BL_COALESCE_FABS)
        the_arena = new CArena;
#else
        the_arena = new BArena;
#endif

#ifdef CUDA
        the_arena->SetPreferred();
#endif

#ifdef CUDA
        const std::size_t hunk_size = 64 * 1024;
        the_nvar_arena = new CArena(hunk_size);
        the_nvar_arena->SetHostAlloc();
#endif

#ifdef _OPENMP
#pragma omp parallel
	{
	    amrex::private_total_bytes_allocated_in_fabs     = 0;
	    amrex::private_total_bytes_allocated_in_fabs_hwm = 0;
	    amrex::private_total_cells_allocated_in_fabs     = 0;
	    amrex::private_total_cells_allocated_in_fabs_hwm = 0;
	}
#endif

#ifdef BL_MEM_PROFILING
	MemProfiler::add("Fab", std::function<MemProfiler::MemInfo()>
			 ([] () -> MemProfiler::MemInfo {
			     return {amrex::TotalBytesAllocatedInFabs(),
				     amrex::TotalBytesAllocatedInFabsHWM()};
			 }));
#endif
    }
}

BF_init::~BF_init ()
{
    if (--m_cnt == 0) {
        delete the_arena;
#ifdef CUDA
        delete the_nvar_arena;
#endif
    }
}

long 
TotalBytesAllocatedInFabs()
{
#ifdef _OPENMP
    long r=0;
#pragma omp parallel reduction(+:r)
    {
	r += private_total_bytes_allocated_in_fabs;
    }
    return r;
#else
    return private_total_bytes_allocated_in_fabs;
#endif
}

long 
TotalBytesAllocatedInFabsHWM()
{
#ifdef _OPENMP
    long r=0;
#pragma omp parallel reduction(+:r)
    {
	r += private_total_bytes_allocated_in_fabs_hwm;
    }
    return r;
#else
    return private_total_bytes_allocated_in_fabs_hwm;
#endif
}

long 
TotalCellsAllocatedInFabs()
{
#ifdef _OPENMP
    long r=0;
#pragma omp parallel reduction(+:r)
    {
	r += private_total_cells_allocated_in_fabs;
    }
    return r;
#else
    return private_total_cells_allocated_in_fabs;
#endif
}

long 
TotalCellsAllocatedInFabsHWM()
{
#ifdef _OPENMP
    long r=0;
#pragma omp parallel reduction(+:r)
    {
	r += private_total_cells_allocated_in_fabs_hwm;
    }
    return r;
#else
    return private_total_cells_allocated_in_fabs_hwm;
#endif
}

void 
ResetTotalBytesAllocatedInFabsHWM()
{
#ifdef _OPENMP
#pragma omp parallel
#endif
    {
	private_total_bytes_allocated_in_fabs_hwm = 0;
    }
}

void
update_fab_stats (long n, long s, size_t szt)
{
    long tst = s*szt;
    amrex::private_total_bytes_allocated_in_fabs += tst;
    amrex::private_total_bytes_allocated_in_fabs_hwm 
	= std::max(amrex::private_total_bytes_allocated_in_fabs_hwm,
		   amrex::private_total_bytes_allocated_in_fabs);
	
    if(szt == sizeof(Real)) {
	amrex::private_total_cells_allocated_in_fabs += n;
	amrex::private_total_cells_allocated_in_fabs_hwm 
	    = std::max(amrex::private_total_cells_allocated_in_fabs_hwm,
		       amrex::private_total_cells_allocated_in_fabs);
    }
}

Arena*
The_Arena ()
{
    BL_ASSERT(the_arena != 0);

    return the_arena;
}

#ifdef CUDA
Arena*
The_Nvar_Arena ()
{
    BL_ASSERT(the_nvar_arena != 0);

    return the_nvar_arena;
}
#endif

#if !defined(BL_NO_FORT)
template<>
void
BaseFab<Real>::performCopy (const BaseFab<Real>& src,
                            const Box&           srcbox,
                            int                  srccomp,
                            const Box&           destbox,
                            int                  destcomp,
                            int                  numcomp)
{
    BL_ASSERT(destbox.ok());
    BL_ASSERT(src.box().contains(srcbox));
    BL_ASSERT(box().contains(destbox));
    BL_ASSERT(destbox.sameSize(srcbox));
    BL_ASSERT(srccomp >= 0 && srccomp+numcomp <= src.nComp());
    BL_ASSERT(destcomp >= 0 && destcomp+numcomp <= nComp());

    Device::prepare_for_launch(destbox.loVect(), destbox.hiVect());

    fort_fab_copy(BL_TO_FORTRAN_BOX(destbox),
		  BL_TO_FORTRAN_N_ANYD(*this,destcomp),
		  BL_TO_FORTRAN_N_ANYD(src,srccomp), ARLIM_3D(srcbox.loVectF()),
		  numcomp);
}

template <>
std::size_t
BaseFab<Real>::copyToMem (const Box& srcbox,
                          int        srccomp,
                          int        numcomp,
                          void*      dst) const
{
    BL_ASSERT(box().contains(srcbox));
    BL_ASSERT(srccomp >= 0 && srccomp+numcomp <= nComp());

    if (srcbox.ok())
    {
	fort_fab_copytomem(BL_TO_FORTRAN_BOX(srcbox),
                           static_cast<Real*>(dst),
                           BL_TO_FORTRAN_N_ANYD(*this,srccomp),
                           numcomp);
        return sizeof(Real) * srcbox.numPts() * numcomp;
    }
    else
    {
        return 0;
    }
}

template <>
std::size_t
BaseFab<Real>::copyFromMem (const Box&  dstbox,
                            int         dstcomp,
                            int         numcomp,
                            const void* src)
{
    BL_ASSERT(box().contains(dstbox));
    BL_ASSERT(dstcomp >= 0 && dstcomp+numcomp <= nComp());

    if (dstbox.ok()) 
    {
	fort_fab_copyfrommem(BL_TO_FORTRAN_BOX(dstbox),
                             BL_TO_FORTRAN_N_ANYD(*this,dstcomp), numcomp,
                             static_cast<const Real*>(src));
        return sizeof(Real) * dstbox.numPts() * numcomp;
    }
    else
    {
        return 0;
    }
}

template<>
void
BaseFab<Real>::performSetVal (Real       val,
                              const Box& bx,
                              int        comp,
                              int        ncomp)
{
    BL_ASSERT(domain.contains(bx));
    BL_ASSERT(comp >= 0 && comp + ncomp <= nvar);

    Device::prepare_for_launch(bx.loVect(), bx.hiVect());

    fort_fab_setval(BL_TO_FORTRAN_BOX(bx),
		    BL_TO_FORTRAN_N_ANYD(*this,comp), ncomp,
		    val);
}

template<>
BaseFab<Real>&
BaseFab<Real>::invert (Real       val,
                       const Box& bx,
                       int        comp,
                       int        ncomp)
{
    BL_ASSERT(domain.contains(bx));
    BL_ASSERT(comp >= 0 && comp + ncomp <= nvar);

    Device::prepare_for_launch(bx.loVect(), bx.hiVect());

    fort_fab_invert(BL_TO_FORTRAN_BOX(bx),
		    BL_TO_FORTRAN_N_ANYD(*this,comp), &ncomp,
		    val);
    return *this;
}

template<>
Real
BaseFab<Real>::norm (const Box& bx,
                     int        p,
                     int        comp,
                     int        ncomp) const
{
    BL_ASSERT(domain.contains(bx));
    BL_ASSERT(comp >= 0 && comp + ncomp <= nvar);

#ifdef CUDA
    Real* nrm_f = Device::create_device_pointer<Real>().get();
#else
    Real nrm;
    Real* nrm_f = &nrm;
#endif

    if (p == 0 || p == 1)
    {
	fort_fab_norm(BL_TO_FORTRAN_BOX(bx),
	              BL_TO_FORTRAN_N_ANYD(*this,comp), ncomp,
		      p, nrm_f);
    }
    else
    {
        amrex::Error("BaseFab<Real>::norm(): only p == 0 or p == 1 are supported");
    }

    return *nrm_f;
}

template<>
Real
BaseFab<Real>::sum (const Box& bx,
                    int        comp,
                    int        ncomp) const
{
    BL_ASSERT(domain.contains(bx));
    BL_ASSERT(comp >= 0 && comp + ncomp <= nvar);

#ifdef CUDA
    Real* sm_f = Device::create_device_pointer<Real>().get();
#else
    Real sm;
    Real* sm_f = &sm;
#endif

    fort_fab_sum(BL_TO_FORTRAN_BOX(bx), BL_TO_FORTRAN_N_ANYD(*this,comp), ncomp, sm_f);

    return *sm_f;
}

template<>
BaseFab<Real>&
BaseFab<Real>::plus (const BaseFab<Real>& src,
                     const Box&           srcbox,
                     const Box&           destbox,
                     int                  srccomp,
                     int                  destcomp,
                     int                  numcomp)
{
    BL_ASSERT(destbox.ok());
    BL_ASSERT(src.box().contains(srcbox));
    BL_ASSERT(box().contains(destbox));
    BL_ASSERT(destbox.sameSize(srcbox));
    BL_ASSERT(srccomp >= 0 && srccomp+numcomp <= src.nComp());
    BL_ASSERT(destcomp >= 0 && destcomp+numcomp <= nComp());

    Device::prepare_for_launch(destbox.loVect(), destbox.hiVect());

    fort_fab_plus(BL_TO_FORTRAN_BOX(destbox),
		  BL_TO_FORTRAN_N_ANYD(*this,destcomp),
		  BL_TO_FORTRAN_N_ANYD(src,srccomp), ARLIM_3D(srcbox.loVectF()),
		  numcomp);

    return *this;
}

template<>
BaseFab<Real>&
BaseFab<Real>::mult (const BaseFab<Real>& src,
                     const Box&           srcbox,
                     const Box&           destbox,
                     int                  srccomp,
                     int                  destcomp,
                     int                  numcomp)
{
    BL_ASSERT(destbox.ok());
    BL_ASSERT(src.box().contains(srcbox));
    BL_ASSERT(box().contains(destbox));
    BL_ASSERT(destbox.sameSize(srcbox));
    BL_ASSERT(srccomp >= 0 && srccomp+numcomp <= src.nComp());
    BL_ASSERT(destcomp >= 0 && destcomp+numcomp <= nComp());

    Device::prepare_for_launch(destbox.loVect(), destbox.hiVect());

    fort_fab_mult(BL_TO_FORTRAN_BOX(destbox),
		  BL_TO_FORTRAN_N_ANYD(*this,destcomp),
		  BL_TO_FORTRAN_N_ANYD(src,srccomp), ARLIM_3D(srcbox.loVectF()),
		  numcomp);
    return *this;
}

template <>
BaseFab<Real>&
BaseFab<Real>::saxpy (Real a, const BaseFab<Real>& src,
                      const Box&        srcbox,
                      const Box&        destbox,
                      int               srccomp,
                      int               destcomp,
                      int               numcomp)
{
    BL_ASSERT(srcbox.ok());
    BL_ASSERT(src.box().contains(srcbox));
    BL_ASSERT(destbox.ok());
    BL_ASSERT(box().contains(destbox));
    BL_ASSERT(destbox.sameSize(srcbox));
    BL_ASSERT( srccomp >= 0 &&  srccomp+numcomp <= src.nComp());
    BL_ASSERT(destcomp >= 0 && destcomp+numcomp <=     nComp());

    Device::prepare_for_launch(destbox.loVect(), destbox.hiVect());

    fort_fab_saxpy(BL_TO_FORTRAN_BOX(destbox),
		   BL_TO_FORTRAN_N_ANYD(*this,destcomp),
		   a,
		   BL_TO_FORTRAN_N_ANYD(src,srccomp), ARLIM_3D(srcbox.loVectF()),
		   numcomp);
    return *this;
}

template <>
BaseFab<Real>&
BaseFab<Real>::xpay (Real a, const BaseFab<Real>& src,
		     const Box&        srcbox,
		     const Box&        destbox,
		     int               srccomp,
		     int               destcomp,
		     int               numcomp)
{
    BL_ASSERT(srcbox.ok());
    BL_ASSERT(src.box().contains(srcbox));
    BL_ASSERT(destbox.ok());
    BL_ASSERT(box().contains(destbox));
    BL_ASSERT(destbox.sameSize(srcbox));
    BL_ASSERT( srccomp >= 0 &&  srccomp+numcomp <= src.nComp());
    BL_ASSERT(destcomp >= 0 && destcomp+numcomp <=     nComp());

    Device::prepare_for_launch(destbox.loVect(), destbox.hiVect());

    fort_fab_xpay(BL_TO_FORTRAN_BOX(destbox),
		  BL_TO_FORTRAN_N_ANYD(*this,destcomp),
		  a,
		  BL_TO_FORTRAN_N_ANYD(src,srccomp), ARLIM_3D(srcbox.loVectF()),
		  numcomp);
    return *this;
}

template <>
BaseFab<Real>&
BaseFab<Real>::addproduct (const Box&           destbox,
			   int                  destcomp,
			   int                  numcomp,
			   const BaseFab<Real>& src1,
			   int                  comp1,
			   const BaseFab<Real>& src2,
			   int                  comp2)
{
    BL_ASSERT(destbox.ok());
    BL_ASSERT(box().contains(destbox));
    BL_ASSERT(   comp1 >= 0 &&    comp1+numcomp <= src1.nComp());
    BL_ASSERT(   comp2 >= 0 &&    comp2+numcomp <= src2.nComp());
    BL_ASSERT(destcomp >= 0 && destcomp+numcomp <=      nComp());

    Device::prepare_for_launch(destbox.loVect(), destbox.hiVect());

    fort_fab_addproduct(BL_TO_FORTRAN_BOX(destbox),
			BL_TO_FORTRAN_N_ANYD(*this,destcomp),
			BL_TO_FORTRAN_N_ANYD(src1,comp1),
			BL_TO_FORTRAN_N_ANYD(src2,comp2),
			numcomp);
    return *this;
}

template<>
BaseFab<Real>&
BaseFab<Real>::minus (const BaseFab<Real>& src,
                      const Box&           srcbox,
                      const Box&           destbox,
                      int                  srccomp,
                      int                  destcomp,
                      int                  numcomp)
{
    BL_ASSERT(destbox.ok());
    BL_ASSERT(src.box().contains(srcbox));
    BL_ASSERT(box().contains(destbox));
    BL_ASSERT(destbox.sameSize(srcbox));
    BL_ASSERT(srccomp >= 0 && srccomp+numcomp <= src.nComp());
    BL_ASSERT(destcomp >= 0 && destcomp+numcomp <= nComp());

    Device::prepare_for_launch(destbox.loVect(), destbox.hiVect());

    fort_fab_minus(BL_TO_FORTRAN_BOX(destbox),
		   BL_TO_FORTRAN_N_ANYD(*this,destcomp),
		   BL_TO_FORTRAN_N_ANYD(src,srccomp), ARLIM_3D(srcbox.loVectF()),
		   numcomp);
    return *this;
}

template<>
BaseFab<Real>&
BaseFab<Real>::divide (const BaseFab<Real>& src,
                       const Box&           srcbox,
                       const Box&           destbox,
                       int                  srccomp,
                       int                  destcomp,
                       int                  numcomp)
{
    BL_ASSERT(destbox.ok());
    BL_ASSERT(src.box().contains(srcbox));
    BL_ASSERT(box().contains(destbox));
    BL_ASSERT(destbox.sameSize(srcbox));
    BL_ASSERT(srccomp >= 0 && srccomp+numcomp <= src.nComp());
    BL_ASSERT(destcomp >= 0 && destcomp+numcomp <= nComp());

    Device::prepare_for_launch(destbox.loVect(), destbox.hiVect());

    fort_fab_divide(BL_TO_FORTRAN_BOX(destbox),
		    BL_TO_FORTRAN_N_ANYD(*this,destcomp),
		    BL_TO_FORTRAN_N_ANYD(src,srccomp), ARLIM_3D(srcbox.loVectF()),
		    numcomp);
    return *this;
}

template<>
BaseFab<Real>&
BaseFab<Real>::protected_divide (const BaseFab<Real>& src,
                                 const Box&           srcbox,
                                 const Box&           destbox,
                                 int                  srccomp,
                                 int                  destcomp,
                                 int                  numcomp)
{
    BL_ASSERT(destbox.ok());
    BL_ASSERT(src.box().contains(srcbox));
    BL_ASSERT(box().contains(destbox));
    BL_ASSERT(destbox.sameSize(srcbox));
    BL_ASSERT(srccomp >= 0 && srccomp+numcomp <= src.nComp());
    BL_ASSERT(destcomp >= 0 && destcomp+numcomp <= nComp());

    Device::prepare_for_launch(destbox.loVect(), destbox.hiVect());

    fort_fab_protdivide(BL_TO_FORTRAN_BOX(destbox),
			BL_TO_FORTRAN_N_ANYD(*this,destcomp),
			BL_TO_FORTRAN_N_ANYD(src,srccomp), ARLIM_3D(srcbox.loVectF()),
			numcomp);
    return *this;
}

template <>
BaseFab<Real>&
BaseFab<Real>::linComb (const BaseFab<Real>& f1,
			const Box&           b1,
			int                  comp1,
			const BaseFab<Real>& f2,
			const Box&           b2,
			int                  comp2,
			Real                 alpha,
			Real                 beta,
			const Box&           b,
			int                  comp,
			int                  numcomp)
{
    BL_ASSERT(b1.ok());
    BL_ASSERT(f1.box().contains(b1));
    BL_ASSERT(b2.ok());
    BL_ASSERT(f2.box().contains(b2));
    BL_ASSERT(b.ok());
    BL_ASSERT(box().contains(b));
    BL_ASSERT(b.sameSize(b1));
    BL_ASSERT(b.sameSize(b2));
    BL_ASSERT(comp1 >= 0 && comp1+numcomp <= f1.nComp());
    BL_ASSERT(comp2 >= 0 && comp2+numcomp <= f2.nComp());
    BL_ASSERT(comp  >= 0 && comp +numcomp <=    nComp());

    Device::prepare_for_launch(b.loVect(), b.hiVect());

    fort_fab_lincomb(BL_TO_FORTRAN_BOX(b),
		     BL_TO_FORTRAN_N_ANYD(*this,comp),
		     alpha, BL_TO_FORTRAN_N_ANYD(f1,comp1), ARLIM_3D(b1.loVectF()),
		     beta,  BL_TO_FORTRAN_N_ANYD(f2,comp2), ARLIM_3D(b2.loVectF()),
		     numcomp);
    return *this;
}

template <>
Real
BaseFab<Real>::dot (const Box& xbx, int xcomp, 
		    const BaseFab<Real>& y, const Box& ybx, int ycomp,
		    int numcomp) const
{
    BL_ASSERT(xbx.ok());
    BL_ASSERT(box().contains(xbx));
    BL_ASSERT(y.box().contains(ybx));
    BL_ASSERT(xbx.sameSize(ybx));
    BL_ASSERT(xcomp >= 0 && xcomp+numcomp <=   nComp());
    BL_ASSERT(ycomp >= 0 && ycomp+numcomp <= y.nComp());

#ifdef CUDA
    Real* dp_f = Device::create_device_pointer<Real>().get();
#else
    Real dp;
    Real* dp_f = &dp;
#endif

    fort_fab_dot(BL_TO_FORTRAN_BOX(xbx),
                 BL_TO_FORTRAN_N_ANYD(*this,xcomp),
                 BL_TO_FORTRAN_N_ANYD(y,ycomp), ARLIM_3D(ybx.loVectF()),
                 numcomp, dp_f);

    return *dp_f;
}

#endif

}
