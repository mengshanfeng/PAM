
#pragma once

#include <cilk/cilk.h>
#include <cilk/cilk_api.h>
#include <iostream>
#include <ctype.h>
#include <memory>
#include <stdlib.h>
#include <type_traits>
#include <type_traits>
#include <math.h>

#define parallel_for cilk_for
//#define parallel_for_1 _Pragma("cilk_grainsize = 1") cilk_for
//#define parallel_for_1 cilk_for
//#define parallel_for_16 _Pragma("cilk_grainsize = 16") cilk_for
//#define parallel_for_256 _Pragma("cilk_grainsize = 256") cilk_for
size_t nworkers() {
  return __cilkrts_get_nworkers();
}

void* my_alloc(size_t);
void my_free(void*);

template <typename Lf, typename Rf>
static void par_do(bool do_parallel, Lf left, Rf right) {
  if (do_parallel) {
    cilk_spawn right();
    left();
    cilk_sync;
  } else {
    left(); right();
  }
}

template <typename Lf, typename Mf, typename Rf >
static void par_do3(bool do_parallel, Lf left, Mf mid, Rf right) {
  if (do_parallel) {
    cilk_spawn mid();
    cilk_spawn right();
    left();
    cilk_sync;
  } else {
    left(); mid(); right();
  }
}

namespace pbbs {
  template <class T>
  size_t log2_up(T);
}
  
template <typename F>
static void par_for(size_t start, size_t end, size_t granularity, F f) {
  if ((end - start) <= granularity)
    for (size_t i=start; i < end; i++) f(i);
  else {
    // picked so not always split in 1/2 since that causes cache conflicts
    // when n is a power of 2
    // size_t mid = (((end - start) < 100)
    // 		  ? (end+start)/2
    // 		  : start + (end-start)/3);
    size_t n = end-start;
    size_t mid = (//(granularity == 1) || (((size_t) 1) << pbbs::log2_up(n) != n)
		  //(n < 16) || (((size_t) 1) << pbbs::log2_up(n) != n)
		  (((size_t) 1) << pbbs::log2_up(n) != n)
		  ? (end+start)/2
		  : start + (7*(n+1))/16);
    cilk_spawn par_for(start, mid, granularity, f);
    par_for(mid, end, granularity, f);
    cilk_sync;
  }
}

template <typename F>
static void par_for(size_t start, size_t end, F f) {
  size_t n = end - start;
  size_t granularity = (n > 100) ? ceil(sqrt(n)) : 100;
  par_for(start, end, granularity, f);
}

template <class T>
struct maybe {
	T value;
	bool valid;
	
	maybe(T v, bool u) : value(v) {
		valid = u;
	}
	maybe(T v) : value(v) {
		valid = true;
	}
	maybe() {
		valid = false;
	}
	
	bool operator !() const {
		return !valid;
	}
	operator bool() const {
		return valid;
	};
	T& operator * () {
		return value;
	}
};

#include <malloc.h>
struct malloc_init {
  static int i;
  static int j; 
};
int malloc_init::i = mallopt(M_MMAP_MAX,0);
int malloc_init::j = mallopt(M_TRIM_THRESHOLD,-1);

namespace pbbs {

  typedef uint32_t flags;
  const flags no_flag = 0;
  const flags fl_sequential = 1;
  const flags fl_debug = 2;
  const flags fl_time = 4;

  template<typename T>
  inline void assign_uninitialized(T& a, const T& b) {
    new (static_cast<void*>(std::addressof(a))) T(b);
  }

  template<typename T>
  inline void move_uninitialized(T& a, const T& b) {
    new (static_cast<void*>(std::addressof(a))) T(std::move(b));
  }

  // a 32-bit hash function
  uint32_t hash32(uint32_t a) {
    a = (a+0x7ed55d16) + (a<<12);
    a = (a^0xc761c23c) ^ (a>>19);
    a = (a+0x165667b1) + (a<<5);
    a = (a+0xd3a2646c) ^ (a<<9);
    a = (a+0xfd7046c5) + (a<<3);
    a = (a^0xb55a4f09) ^ (a>>16);
    return a;
  }

  // from numerical recipes
  uint64_t hash64(uint64_t u )
  {
    uint64_t v = u * 3935559000370003845ul + 2691343689449507681ul;
    v ^= v >> 21;
    v ^= v << 37;
    v ^= v >>  4;
    v *= 4768777513237032717ul;
    v ^= v << 20;
    v ^= v >> 41;
    v ^= v <<  5;
    return v;
  }

  // a slightly cheaper, but possibly not as good version
  // based on splitmix64
  uint64_t hash64_2(uint64_t x) {
    x = (x ^ (x >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    x = (x ^ (x >> 27)) * UINT64_C(0x94d049bb133111eb);
    x = x ^ (x >> 31);
    return x;
  }

  // Does not initialize the array
  template<typename E>
    E* new_array_no_init(size_t n, bool touch_pages=false) { //true) {
    // pads in case user wants to allign with cache lines
    size_t line_size = 64;
    size_t bytes = ((n * sizeof(E))/line_size + 1)*line_size;
    //E* r = (E*) aligned_alloc(line_size, bytes);
    E* r = (E*) my_alloc(bytes);
    if (r == NULL) {fprintf(stderr, "Cannot allocate space: %lu bytes", bytes); exit(1);}
    //parallel_for (size_t i = 0; i < bytes; i = i + (1 << 21)) ((bool*) r)[i] = 0;
    return r;
  }
  
  // Initializes in parallel
  template<typename E>
  E* new_array(size_t n) {
    E* r = new_array_no_init<E>(n);
    if (!std::is_trivially_default_constructible<E>::value) {
    //if (!std::is_default_constructible<E>::value) {
      if (n > 2048) {
	auto f = [&] (size_t i) { new ((void*) (r+i)) E;};
	par_for(0, n, f);
	//parallel_for (size_t i = 0; i < n; i++) new ((void*) (r+i)) E;
      }
      else
	for (size_t i = 0; i < n; i++) new ((void*) (r+i)) E;
    }
    return r;
  }

  void free_array(void* a) {
    //free(A);
    my_free(a);
  }
  
  // Destructs in parallel 
  template<typename E>
  void delete_array(E* A, size_t n) {
    // C++14 -- suppored by gnu C++11
    if (!std::is_trivially_destructible<E>::value) {
      //if (!std::is_destructible<E>::value) {  
      if (n > 2048) {
	auto f = [&] (size_t i) {A[i].~E();};
	par_for(0, n, f);
	//parallel_for (size_t i = 0; i < n; i++) A[i].~E();
      } else for (size_t i = 0; i < n; i++) A[i].~E();
    }
    //free(A);
    my_free(A);
  }

  template <typename ET>
  inline bool atomic_compare_and_swap(ET* ptr, const ET oldv, const ET newv) {
    return __sync_bool_compare_and_swap(ptr, oldv, newv);
  }

  template <typename ET>
  inline bool CAS_GCC(ET* ptr, const ET oldv, const ET newv) {
    return __sync_bool_compare_and_swap(ptr, oldv, newv);
  }

  inline bool atomic_compare_and_swap(double* a, const double &oldval, const double &newval) {
    return __sync_bool_compare_and_swap(reinterpret_cast<uint64_t*>(a),
					*reinterpret_cast<const uint64_t*>(&oldval),
					*reinterpret_cast<const uint64_t*>(&newval));
  };

  inline bool atomic_compare_and_swap(float* a, float &oldval, float &newval) {
    return __sync_bool_compare_and_swap(reinterpret_cast<uint32_t*>(a),
					*reinterpret_cast<const uint32_t*>(&oldval),
					*reinterpret_cast<const uint32_t*>(&newval));
  };
      
  template <typename E, typename EV>
  inline E fetch_and_add(E *a, EV b) {
    volatile E newV, oldV; 
    do {oldV = *a; newV = oldV + b;}
    while (!CAS_GCC(a, oldV, newV));
    return oldV;
  }

  template <typename E, typename EV>
  inline void write_add(E *a, EV b) {
    //volatile E newV, oldV;
    E newV, oldV; 
    do {oldV = *a; newV = oldV + b;}
    while (!atomic_compare_and_swap(a, oldV, newV));
  }

  template <typename ET, typename F>
  inline bool write_min(ET *a, ET b, F less) {
    ET c; bool r=0;
    do c = *a; 
    while (less(b,c) && !(r=CAS_GCC(a,c,b)));
    return r;
  }

  // returns the log base 2 rounded up (works on ints or longs or unsigned versions)
  template <class T>
  size_t log2_up(T i) {
    size_t a=0;
    T b=i-1;
    while (b > 0) {b = b >> 1; a++;}
    return a;
  }

  size_t granularity(size_t n) {
    return (n > 100) ? ceil(pow(n,0.5)) : 100;
  }

}


#include "alloc.h"
