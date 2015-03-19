/******************************************************************************
 * succinct_tree.c
 *
 * Parallel construction of succinct trees
 * For more information: http://www.inf.udec.cl/~josefuentes/sea2015/
 *
 ******************************************************************************
 * Copyright (C) 2015 José Fuentes Sepúlveda <jfuentess@udec.cl>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#ifdef NOPARALLEL
#define cilk_for for
#define cilk_spawn
#define cilk_sync
#define __cilkrts_get_nworkers() 1
#else
#include <cilk/cilk.h>
#include <cilk/cilk_api.h>
#include <cilk/common.h>
#endif

#include "lookup_tables.h"
#include "binary_trees.h"
#include "succinct_tree.h"


#define min(a,b) \
  ({ __typeof__ (a) _a = (a); \
      __typeof__ (b) _b = (b); \
    _a < _b ? _a : _b; })

#define max(a,b) \
  ({ __typeof__ (a) _a = (a); \
      __typeof__ (b) _b = (b); \
    _a > _b ? _a : _b; })


/* ASSUMPTIONS:
 * - s = 256 (8 bits) (Following the sdsl/libcds implementations)
 * - k = 2 (Min-max tree will be a binary tree)
 * - Each thread has to process at least one chunk with parentheses (Problem with n <= s)
 */

unsigned int s = 256; // Chunk size
unsigned int k = 2; // arity of the min-max tree
int16_t* e_prime; // num_chunks leaves (it does not need internal nodes)
int16_t* m_prime; // num_chunks leaves plus offset internal nodes
int16_t* M_prime; // num_chunks leaves plus offset internal nodes
int16_t* n_prime; // num_chunks leaves plus offset internal nodes


void st_create(BIT_ARRAY* B, unsigned long n) {
  if(s>=n){
    fprintf(stderr, "Error: Input size is smaller or equal than the chunk size (input size: %lu, chunk size: %u)\n", n, s);
    exit(0);
  }
  
  /*
   * STEP 1: Computation of the balanced-parentheses sequence (See PFEA algorithm)
   * B corresponds to the balanced-parentheses sequence
   */
  
  /*
   * STEP 2: Computation of arrays e', m', M' and n'
   */
#ifdef ARCH64
  long num_chunks = ceil((double)n / s);
#else
  unsigned int num_chunks = ceil((double)n / s);
#endif
  
  unsigned int num_threads = __cilkrts_get_nworkers();
  heigh = ceil(log(num_chunks)/log(k)); // heigh = logk(num_chunks), Heigh of the min-max tree
  unsigned int offset = (pow(k,heigh)-1)/(k-1); // Number of internal nodes
  unsigned int chunks_per_thread = ceil((double)num_chunks/num_threads); // Each thread works on 'chunks_per_thread' consecutive chunks of B
  e_prime = (int16_t*)calloc(num_chunks,sizeof(int16_t)); // num_chunks leaves (it does not need internal nodes)
  m_prime = (int16_t*)calloc(num_chunks + offset,sizeof(int16_t)); // num_chunks leaves plus offset internal nodes
  M_prime = (int16_t*)calloc(num_chunks + offset,sizeof(int16_t)); // num_chunks leaves plus offset internal nodes
  n_prime = (int16_t*)calloc(num_chunks + offset,sizeof(int16_t)); // num_chunks leaves plus offset internal nodes
  
  /*
   * STEP 2.1: Each thread computes the prefix computation in a range of the bit array
   */

  unsigned int thread = 0;
  
  cilk_for(thread=0; thread < num_threads; thread++) {
    unsigned int chunk = 0;
    unsigned chunk_limit; // It is possible that the last thread process less chunks
    
    if((thread == num_threads - 1) && (num_chunks%chunks_per_thread != 0))
      chunk_limit = num_chunks%chunks_per_thread;
    else
      chunk_limit = chunks_per_thread;
    
    int16_t min = 0, max = 0, partial_excess = 0;
    
    // Each thread traverses their chunks
    for(chunk=0; chunk<chunk_limit; chunk++){
      int16_t num_mins = 1; // Number of occurrences of the minimum value in the chunk
      unsigned int llimit = 0, ulimit = 0;
      
      // Compute the limits of the current chunk
      if(thread == (num_threads - 1) && chunk == (chunk_limit-1) && n % (num_chunks * s) != 0){
	llimit = thread*chunks_per_thread*s+(s*chunk);
	ulimit = n;
      }
      else {
	llimit = thread*chunks_per_thread*s + (s*chunk);
	ulimit = thread*chunks_per_thread*s + (s*(chunk+1));
	if(n<s)
	  ulimit =n;
      }
      
      unsigned int symbol=0;
      for(symbol=llimit; symbol<ulimit; symbol++) {
	// Excess computation
	if(bit_array_get_bit(B, symbol) == 0)
	  --partial_excess;
	else
	  ++partial_excess;
	
	// Minimum computation
	if(symbol==llimit) {
	  min = partial_excess; // By default the minimum value is the first excess value
	  max = partial_excess; // By default the maximum value is the first excess value
	  num_mins = 1;
	}
	else {
	  if(partial_excess < min) {
	    min = partial_excess;
	    num_mins = 1;
	  } else if(partial_excess == min)
	    num_mins++;
	  
	  if(partial_excess > max)
	    max = partial_excess;
	  
	}	
      }

      e_prime[thread*chunks_per_thread+chunk] = partial_excess;
      m_prime[offset + thread*chunks_per_thread+chunk] = min;
      M_prime[offset + thread*chunks_per_thread+chunk] = max;
      n_prime[offset + thread*chunks_per_thread+chunk] = num_mins;
    }
  }
  
  /*
   * STEP 2.2: Computation of the final prefix computations (desired values)
   */
  
  for(thread=1; thread < num_threads-1; thread++) {
    e_prime[thread*chunks_per_thread+chunks_per_thread-1] += e_prime[(thread-1)*chunks_per_thread+chunks_per_thread-1];
  }  
  
  cilk_for(thread=1; thread < num_threads; thread++) {
    unsigned int chunk = 0;
    /*
     * Note 1: Thread 0 does not need to update their excess values
     * Note 2:Thread 0 does not need to update the minimum value of its first chunk
     */
    for(chunk=0; chunk<chunks_per_thread; chunk++){
      if((thread == num_threads-1) || (chunk < chunks_per_thread -1))
	e_prime[thread*chunks_per_thread+chunk] += e_prime[(thread-1)*chunks_per_thread+chunks_per_thread-1];
      m_prime[offset + thread*chunks_per_thread+chunk] += e_prime[(thread-1)*chunks_per_thread+chunks_per_thread-1];
      M_prime[offset + thread*chunks_per_thread+chunk] += e_prime[(thread-1)*chunks_per_thread+chunks_per_thread-1];
      
    }
  }
  
  /*
   * STEP 2.3: Completing the internal nodes of the min-max tree
   */
      
  int p_level = ceil(log(num_threads)/log(k)); /* p_level = logk(num_threads), level at which each thread has at least one 
						  subtree to process in parallel */
  unsigned int num_subtrees = pow(k,p_level); /* num_subtrees = k^p_level, number of subtrees of the min-max tree 
						 that will be computed in parallel at level p_level.
						 num_subtrees is O(num_threads) */
  
  unsigned int subtree = 0;
  
  cilk_for(subtree = 0; subtree < num_subtrees; subtree++) {
    int lvl = 0;
    for(lvl = heigh-1; lvl >= p_level; lvl--){ /* The current level that is being constructed.
						  Note: The last level (leaves) is already constructed */
      unsigned int num_curr_nodes = pow(k, lvl-p_level); /* Number of nodes at curr_level level that belong to the subtree */
      unsigned int node = 0, child = 0;
      
      for(node = 0; node < num_curr_nodes; node++) {
	unsigned int pos = pow(k,lvl)-1 + node + subtree*num_curr_nodes; /* Position in the final array of 'node'.
									    Note: It should be less than the offset */
	unsigned int lchild = pos*k+1, rchild = (pos+1)*k; /* Range of children of 'node' in the final array */
	
	for(child = lchild; (child <= rchild) && (child < n); child++){
	  if(child == lchild){ // first time
	    m_prime[pos] = m_prime[child];
	    M_prime[pos] = M_prime[child];
	    n_prime[pos] = n_prime[child];
	  }
	  else {
	    if(m_prime[child] < m_prime[pos]) {
	      m_prime[pos] = m_prime[child];
	      n_prime[pos] = 1;
	    }
	    else if(m_prime[child] == m_prime[pos])
	      n_prime[pos]++;
	    
	    if(M_prime[child] > M_prime[pos])
	      M_prime[pos] = M_prime[child];
	  }
	}
      }
    }
  }
   
  int lvl = 0;
  for(lvl=p_level-1; lvl >= 0 ; lvl--){ // O(num_threads)
    
    unsigned int num_curr_nodes = pow(k, lvl); // Number of nodes at curr_level level that belong to the subtree
    unsigned int node = 0, child = 0;
    
    for(node = 0; node < num_curr_nodes; node++) {
      unsigned int pos = (pow(k,lvl)-1)/(k-1) + node; // Position in the final array of 'node'
      unsigned int lchild = pos*k+1, rchild = (pos+1)*k; // Range of children of 'node' in the final array
      for(child = lchild; (child <= rchild) && (child < n); child++){
	if(child == lchild){ // first time
	  m_prime[pos] = m_prime[child];
	  M_prime[pos] = M_prime[child];
	}
	else {
	  if(m_prime[child] < m_prime[pos]) {
	    m_prime[pos] = m_prime[child];
	    n_prime[pos] = 1;
	  }
	  else if(m_prime[child] == m_prime[pos])
	    n_prime[pos]++;
	  
	  if(M_prime[child] > M_prime[pos])
	    M_prime[pos] = M_prime[child];
	}
      }
    }  
  }
  
  /*
   * STEP 3: Computation of all universal tables
   */
  
  T = create_lookup_tables();
   
}


/* TODO: Test this function*/
int32_t leaves_check(BIT_ARRAY* B, int32_t i, int8_t d){
  int end = (i/s+1)*s;
  int llimit = (((i)+7)/8)*8;
  int rlimit = (end/8)*8;
  int8_t excess = d;
  int32_t output;
  int32_t j = 0;
  
  for(j=i+1; j<min(end, llimit); j++){
    excess += 1-2*bit_array_get_bit(B,j);
    if(excess == 0)
      return j;
  }
  
  if(llimit == i)
    excess+=9;
  else
    excess += 8;
  
  for(j=llimit; j<rlimit; j+=8){
    if (excess >= 0 && excess <= 16) {
      uint16_t i = (excess<<8) + (((*(B->words+(j>>5)))>>(j&0xFF))&0xFF);
      
      int8_t x = T->near_fwd_pos[i];
      if(x < 8)
	return j+x;
    }
    excess -= T->word_sum[((*(B->words+(j>>5)))>>(j&0xFF))&0xFF ];
  }
  
  excess -= 8;
  for (j=max(llimit,rlimit); j < end; ++j) {
    excess += 1-2*bit_array_get_bit(B,j);
    if (excess == 0) {
      return j;
    }
  }
  
  return i-1;
}

/* TODO: Test this function*/
int32_t fwd_search(BIT_ARRAY* B, int32_t i, int8_t excess){
  int chunk = floor((double)i / s);
  int32_t output;
  long j;
  
  // Case 1: Check if the chunk of i contains fwd_search(B, i, d)
  
  output = leaves_check(B, i, excess);
  if(output > i)
    return output;
  
  // Case 2: The answer is not in the chunk of i, but it is in a sibling
  
  // Global target value
  // sum(P,g,i,rk): it should be done with look-up tables or rank/select estructures
  int8_t prev_excess=0;

  for(j=chunk*s; j <= i; j++)
    prev_excess += 1-2*bit_array_get_bit(B,j);
  
  if(chunk == 0)
    excess = prev_excess;
  else
    excess = -e_prime[chunk-1] + prev_excess;
  
  unsigned int pos_med_block =chunk%k;
  
  for(j=pos_med_block+1; j<k; j++){
    if(m_prime[chunk+j] <= (-excess-1) && (-excess-1) <= M_prime[chunk+j]){
      output = leaves_check(B, s*(chunk+j), excess);
      if(output > s*(chunk+j))
	return output;
    }
  }
  
  // Case 3: It is necessary up and then down in the min-max tree
  
  long node = (pow(k,heigh)-1) + j -1; // Initial node
  // Go up the tree
  while (!is_root(node)) {
    if (is_left_child(node)) { // if the node is a left child
      node = right_sibling(node); // choose right sibling
      
      if (m_prime[node] <= -excess-1 && -excess-1 <= M_prime[node])
	break;
    }
    node = parent(node); // choose parent
  }
  
  // Go down the tree
  if (!is_root(node)) { // found solution for the query
    while (!is_leaf(node)) {
      node = left_child(node); // choose left child
      if (!(m_prime[node] <= (-excess-1) && (-excess-1) <= M_prime[node])) {
	node = right_sibling(node); // choose right child == right sibling of the left child
	if(m_prime[node] > -excess-1 || -excess-1 > M_prime[node]){
	  exit(EXIT_FAILURE);
	}
      }
    }

    chunk = node - (pow(k,heigh)-1);
    excess = -e_prime[chunk-1] + excess;
    
    return leaves_check(B, s*chunk, excess);
  }
}

/* TODO: Test this function*/
int32_t find_close(BIT_ARRAY* B, int32_t i){
  return fwd_search(B, i, -1);
}