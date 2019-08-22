#ifndef ALGEBRA_HH
#define ALGEBRA_HH

#include "types.hh"

static inline
bool index_exists(pRow_t r, Index_t icol)
{
    bool result = false;
    Row_t::iterator rit = r->begin();
    while (rit != r->end())
    {
        if (icol == std::get<0>(*rit))
        {
            result = true;
            break;
        }
        ++rit;
    }
    return result;
}

static inline
bool dot(Scalar_t & ans, pRow_t a, pRow_t b) // no semiring
{
    bool result = false;
    Row_t::iterator ait = a->begin();
    Row_t::iterator bit = b->begin();

    ans = 0;
    while (ait != a->end() && bit != b->end())
    {
        Index_t a_idx = std::get<0>(*ait);
        Index_t b_idx = std::get<0>(*bit);

        if (a_idx == b_idx)
        {
            ans += std::get<1>(*ait) * std::get<1>(*bit);
            result = true;
            ++ait;
            ++bit;
        }
        else if (a_idx < b_idx)
        {
            ++ait;
        }
        else
        {
            ++bit;
        }
    }
    return result;
}

static inline
void copy_remote_row(pRow_t src, Row_t & dest)
{
    Index_t sz = src->size(); // migrate to get size
    dest.resize(sz);          // migrate back to resize and copy
    memcpy(dest.data(), src->data(), sz*sizeof(std::tuple<Index_t, Scalar_t>));
}

static inline
void row_kernel(Index_t irow,
                prMatrix_t C,
                prMatrix_t const M,
                prMatrix_t const A,
                prMatrix_t const B)
{
    // return for empty row of A
    if (!A->getrow(irow)) return;

    // loop over columns
    for (Index_t icol = 0; icol < A->nrows(); ++icol)
    {
        // continue for empty column of B
        if (!B->getrow(icol)) continue;
        // apply mask
        if (!index_exists(M->getrow(irow), icol)) continue;

        // copy the remote row into r
        Row_t r;
        copy_remote_row(B->getrow(icol), r);

        // compute the dot
        Scalar_t ans;
        if (dot(ans, A->getrow(irow), &r))
        {
            C->getrow(irow)->push_back(std::make_tuple(icol, ans));
        }
    }
}

static inline
void multi_row_kernel(Index_t t,
                      Index_t nrow,
                      prMatrix_t C,
                      prMatrix_t const M,
                      prMatrix_t const A,
                      prMatrix_t const B)
{
    for (Index_t j = t*nrow; j < (t+1)*nrow; ++j)
    {
        // absolute row index
        Index_t irow = nr_inv(NODE_ID(), j);
        row_kernel(irow, C, M, A, B);
    }

}

static inline
void ABT_Mask_NoAccum_kernel(
    prMatrix_t C,               // output matrix
    prMatrix_t const M,         // mask matrix
    // SemiringT,               // semiring
    prMatrix_t const A,         // Input matrix 1
    prMatrix_t const B,         // Input matrix 2
    bool replace_flag = false)  // put the answer in place?
{
    // making use of the fact we know that B equals L^T

    // compute rows per thread
    Index_t threads_per_nodelet =
        THREAD_OVERSUBSCRIBE * THREADS_PER_GC * GC_PER_NODELET;
    Index_t nrows_per_thread = A->nrows_nl() / threads_per_nodelet;
    // if nrows_nl < threads_per_nodelet, all rows are remainder rows
    Index_t nremainder_rows = A->nrows_nl() % threads_per_nodelet;

    // spawn threads_per_nodelet threads
    if (nrows_per_thread)
    {
        for (Index_t t = 0; t < threads_per_nodelet; ++t)
        {
            cilk_spawn multi_row_kernel(t, nrows_per_thread, C, M, A, B);
        }
        cilk_sync;
    }

    // spawn nremainder_rows threads
    if (nremainder_rows)
    {
        Index_t offset = nrows_per_thread * threads_per_nodelet;
        for (Index_t t = 0; t < nremainder_rows; ++t)
        {
            // absolute row index
            Index_t irow = nr_inv(NODE_ID(), t + offset);
            cilk_spawn row_kernel(irow, C, M, A, B);
        }
        cilk_sync;
    }
}

Scalar_t reduce(prMatrix_t A)
{
    Scalar_t sum = 0;
    for (Index_t irow = 0; irow < A->nrows(); ++irow)
    {
        pRow_t pArow = A->getrow(irow);
        Row_t::iterator ait = pArow->begin();
        while (ait != pArow->end())
        {
            sum += std::get<1>(*ait);
            ++ait;
        }
    }
    return sum;
}

#endif // ALGEBRA_HH
