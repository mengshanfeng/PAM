#include "utilities.h"

// multiply a compresses sparse row matrix
template <class IntSeq, class Seq, class Mult, class Add>
void mat_vec_mult(IntSeq starts, IntSeq columns,
		  Seq values, Seq in, Seq out,
		  Mult mult, Add add) {
  using E = typename Seq::T;
  size_t n = in.size();
  auto row_f = [&] (size_t i) {
    size_t s = starts[i];
    size_t e = starts[i+1];
    if (e > s) {
      E sum = mult(in[columns[s]],values[s]);
      for (size_t j=s+1; j < e; j++)
	sum = add(sum,mult(in[columns[j]],values[j]));
      out[i] = sum;
    } else out[i] = 0;
  };
  parallel_for(0, n, row_f, pbbs::granularity(n));
}
