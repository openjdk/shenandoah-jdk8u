/*
 * Copyright (c) 2007, 2013, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 */

#ifndef SHARE_VM_OPTO_SUPERWORD_HPP
#define SHARE_VM_OPTO_SUPERWORD_HPP

#include "opto/connode.hpp"
#include "opto/loopnode.hpp"
#include "opto/node.hpp"
#include "opto/phaseX.hpp"
#include "opto/vectornode.hpp"
#include "utilities/growableArray.hpp"

#ifdef AIX
#include "cstdlib"
#endif

//
//                  S U P E R W O R D   T R A N S F O R M
//
// SuperWords are short, fixed length vectors.
//
// Algorithm from:
//
// Exploiting SuperWord Level Parallelism with
//   Multimedia Instruction Sets
// by
//   Samuel Larsen and Saman Amarasinghe
//   MIT Laboratory for Computer Science
// date
//   May 2000
// published in
//   ACM SIGPLAN Notices
//   Proceedings of ACM PLDI '00,  Volume 35 Issue 5
//
// Definition 3.1 A Pack is an n-tuple, <s1, ...,sn>, where
// s1,...,sn are independent isomorphic statements in a basic
// block.
//
// Definition 3.2 A PackSet is a set of Packs.
//
// Definition 3.3 A Pair is a Pack of size two, where the
// first statement is considered the left element, and the
// second statement is considered the right element.

class SWPointer;
class OrderedPair;

// ========================= Dependence Graph =====================

class DepMem;

//------------------------------DepEdge---------------------------
// An edge in the dependence graph.  The edges incident to a dependence
// node are threaded through _next_in for incoming edges and _next_out
// for outgoing edges.
class DepEdge : public ResourceObj {
 protected:
  DepMem* _pred;
  DepMem* _succ;
  DepEdge* _next_in;   // list of in edges, null terminated
  DepEdge* _next_out;  // list of out edges, null terminated

 public:
  DepEdge(DepMem* pred, DepMem* succ, DepEdge* next_in, DepEdge* next_out) :
    _pred(pred), _succ(succ), _next_in(next_in), _next_out(next_out) {}

  DepEdge* next_in()  { return _next_in; }
  DepEdge* next_out() { return _next_out; }
  DepMem*  pred()     { return _pred; }
  DepMem*  succ()     { return _succ; }

  void print();
};

//------------------------------DepMem---------------------------
// A node in the dependence graph.  _in_head starts the threaded list of
// incoming edges, and _out_head starts the list of outgoing edges.
class DepMem : public ResourceObj {
 protected:
  Node*    _node;     // Corresponding ideal node
  DepEdge* _in_head;  // Head of list of in edges, null terminated
  DepEdge* _out_head; // Head of list of out edges, null terminated

 public:
  DepMem(Node* node) : _node(node), _in_head(NULL), _out_head(NULL) {}

  Node*    node()                { return _node;     }
  DepEdge* in_head()             { return _in_head;  }
  DepEdge* out_head()            { return _out_head; }
  void set_in_head(DepEdge* hd)  { _in_head = hd;    }
  void set_out_head(DepEdge* hd) { _out_head = hd;   }

  int in_cnt();  // Incoming edge count
  int out_cnt(); // Outgoing edge count

  void print();
};

//------------------------------DepGraph---------------------------
class DepGraph VALUE_OBJ_CLASS_SPEC {
 protected:
  Arena* _arena;
  GrowableArray<DepMem*> _map;
  DepMem* _root;
  DepMem* _tail;

 public:
  DepGraph(Arena* a) : _arena(a), _map(a, 8,  0, NULL) {
    _root = new (_arena) DepMem(NULL);
    _tail = new (_arena) DepMem(NULL);
  }

  DepMem* root() { return _root; }
  DepMem* tail() { return _tail; }

  // Return dependence node corresponding to an ideal node
  DepMem* dep(Node* node) { return _map.at(node->_idx); }

  // Make a new dependence graph node for an ideal node.
  DepMem* make_node(Node* node);

  // Make a new dependence graph edge dprec->dsucc
  DepEdge* make_edge(DepMem* dpred, DepMem* dsucc);

  DepEdge* make_edge(Node* pred,   Node* succ)   { return make_edge(dep(pred), dep(succ)); }
  DepEdge* make_edge(DepMem* pred, Node* succ)   { return make_edge(pred,      dep(succ)); }
  DepEdge* make_edge(Node* pred,   DepMem* succ) { return make_edge(dep(pred), succ);      }

  void init() { _map.clear(); } // initialize

  void print(Node* n)   { dep(n)->print(); }
  void print(DepMem* d) { d->print(); }
};

//------------------------------DepPreds---------------------------
// Iterator over predecessors in the dependence graph and
// non-memory-graph inputs of ideal nodes.
class DepPreds : public StackObj {
private:
  Node*    _n;
  int      _next_idx, _end_idx;
  DepEdge* _dep_next;
  Node*    _current;
  bool     _done;

public:
  DepPreds(Node* n, DepGraph& dg);
  Node* current() { return _current; }
  bool  done()    { return _done; }
  void  next();
};

//------------------------------DepSuccs---------------------------
// Iterator over successors in the dependence graph and
// non-memory-graph outputs of ideal nodes.
class DepSuccs : public StackObj {
private:
  Node*    _n;
  int      _next_idx, _end_idx;
  DepEdge* _dep_next;
  Node*    _current;
  bool     _done;

public:
  DepSuccs(Node* n, DepGraph& dg);
  Node* current() { return _current; }
  bool  done()    { return _done; }
  void  next();
};


// ========================= SuperWord =====================

// -----------------------------SWNodeInfo---------------------------------
// Per node info needed by SuperWord
class SWNodeInfo VALUE_OBJ_CLASS_SPEC {
 public:
  int         _alignment; // memory alignment for a node
  int         _depth;     // Max expression (DAG) depth from block start
  const Type* _velt_type; // vector element type
  Node_List*  _my_pack;   // pack containing this node

  SWNodeInfo() : _alignment(-1), _depth(0), _velt_type(NULL), _my_pack(NULL) {}
  static const SWNodeInfo initial;
};

// JVMCI: OrderedPair is moved up to deal with compilation issues on Windows
//------------------------------OrderedPair---------------------------
// Ordered pair of Node*.
class OrderedPair VALUE_OBJ_CLASS_SPEC {
 protected:
  Node* _p1;
  Node* _p2;
 public:
  OrderedPair() : _p1(NULL), _p2(NULL) {}
  OrderedPair(Node* p1, Node* p2) {
    if (p1->_idx < p2->_idx) {
      _p1 = p1; _p2 = p2;
    } else {
      _p1 = p2; _p2 = p1;
    }
  }

  bool operator==(const OrderedPair &rhs) {
    return _p1 == rhs._p1 && _p2 == rhs._p2;
  }
  void print() { tty->print("  (%d, %d)", _p1->_idx, _p2->_idx); }

  static const OrderedPair initial;
};

// -----------------------------SuperWord---------------------------------
// Transforms scalar operations into packed (superword) operations.
class SuperWord : public ResourceObj {
 friend class SWPointer;
 private:
  PhaseIdealLoop* _phase;
  Arena*          _arena;
  PhaseIterGVN   &_igvn;

  enum consts { top_align = -1, bottom_align = -666 };

  GrowableArray<Node_List*> _packset;    // Packs for the current block

  GrowableArray<int> _bb_idx;            // Map from Node _idx to index within block

  GrowableArray<Node*> _block;           // Nodes in current block
  GrowableArray<Node*> _data_entry;      // Nodes with all inputs from outside
  GrowableArray<Node*> _mem_slice_head;  // Memory slice head nodes
  GrowableArray<Node*> _mem_slice_tail;  // Memory slice tail nodes

  GrowableArray<SWNodeInfo> _node_info;  // Info needed per node

  MemNode* _align_to_ref;                // Memory reference that pre-loop will align to

  GrowableArray<OrderedPair> _disjoint_ptrs; // runtime disambiguated pointer pairs

  DepGraph _dg; // Dependence graph

  // Scratch pads
  VectorSet    _visited;       // Visited set
  VectorSet    _post_visited;  // Post-visited set
  Node_Stack   _n_idx_list;    // List of (node,index) pairs
  GrowableArray<Node*> _nlist; // List of nodes
  GrowableArray<Node*> _stk;   // Stack of nodes

 public:
  SuperWord(PhaseIdealLoop* phase);

  void transform_loop(IdealLoopTree* lpt);

  // Accessors for SWPointer
  PhaseIdealLoop* phase()          { return _phase; }
  IdealLoopTree* lpt()             { return _lpt; }
  PhiNode* iv()                    { return _iv; }

 private:
  IdealLoopTree* _lpt;             // Current loop tree node
  LoopNode*      _lp;              // Current LoopNode
  Node*          _bb;              // Current basic block
  PhiNode*       _iv;              // Induction var

  // Accessors
  Arena* arena()                   { return _arena; }

  Node* bb()                       { return _bb; }
  void  set_bb(Node* bb)           { _bb = bb; }

  void set_lpt(IdealLoopTree* lpt) { _lpt = lpt; }

  LoopNode* lp()                   { return _lp; }
  void      set_lp(LoopNode* lp)   { _lp = lp;
                                     _iv = lp->as_CountedLoop()->phi()->as_Phi(); }
  int      iv_stride()             { return lp()->as_CountedLoop()->stride_con(); }

  int vector_width(Node* n) {
    BasicType bt = velt_basic_type(n);
    return MIN2(ABS(iv_stride()), Matcher::max_vector_size(bt));
  }
  int vector_width_in_bytes(Node* n) {
    BasicType bt = velt_basic_type(n);
    return vector_width(n)*type2aelembytes(bt);
  }
  MemNode* align_to_ref()            { return _align_to_ref; }
  void  set_align_to_ref(MemNode* m) { _align_to_ref = m; }

  Node* ctrl(Node* n) const { return _phase->has_ctrl(n) ? _phase->get_ctrl(n) : n; }

  // block accessors
  bool in_bb(Node* n)      { return n != NULL && n->outcnt() > 0 && ctrl(n) == _bb; }
  int  bb_idx(Node* n)     { assert(in_bb(n), "must be"); return _bb_idx.at(n->_idx); }
  void set_bb_idx(Node* n, int i) { _bb_idx.at_put_grow(n->_idx, i); }

  // visited set accessors
  void visited_clear()           { _visited.Clear(); }
  void visited_set(Node* n)      { return _visited.set(bb_idx(n)); }
  int visited_test(Node* n)      { return _visited.test(bb_idx(n)); }
  int visited_test_set(Node* n)  { return _visited.test_set(bb_idx(n)); }
  void post_visited_clear()      { _post_visited.Clear(); }
  void post_visited_set(Node* n) { return _post_visited.set(bb_idx(n)); }
  int post_visited_test(Node* n) { return _post_visited.test(bb_idx(n)); }

  // Ensure node_info contains element "i"
  void grow_node_info(int i) { if (i >= _node_info.length()) _node_info.at_put_grow(i, SWNodeInfo::initial); }

  // memory alignment for a node
  int alignment(Node* n)                     { return _node_info.adr_at(bb_idx(n))->_alignment; }
  void set_alignment(Node* n, int a)         { int i = bb_idx(n); grow_node_info(i); _node_info.adr_at(i)->_alignment = a; }

  // Max expression (DAG) depth from beginning of the block for each node
  int depth(Node* n)                         { return _node_info.adr_at(bb_idx(n))->_depth; }
  void set_depth(Node* n, int d)             { int i = bb_idx(n); grow_node_info(i); _node_info.adr_at(i)->_depth = d; }

  // vector element type
  const Type* velt_type(Node* n)             { return _node_info.adr_at(bb_idx(n))->_velt_type; }
  BasicType velt_basic_type(Node* n)         { return velt_type(n)->array_element_basic_type(); }
  void set_velt_type(Node* n, const Type* t) { int i = bb_idx(n); grow_node_info(i); _node_info.adr_at(i)->_velt_type = t; }
  bool same_velt_type(Node* n1, Node* n2);

  // my_pack
  Node_List* my_pack(Node* n)                { return !in_bb(n) ? NULL : _node_info.adr_at(bb_idx(n))->_my_pack; }
  void set_my_pack(Node* n, Node_List* p)    { int i = bb_idx(n); grow_node_info(i); _node_info.adr_at(i)->_my_pack = p; }

  // methods

  // Extract the superword level parallelism
  void SLP_extract();
  // Find the adjacent memory references and create pack pairs for them.
  void find_adjacent_refs();
  // Find a memory reference to align the loop induction variable to.
  MemNode* find_align_to_ref(Node_List &memops);
  // Calculate loop's iv adjustment for this memory ops.
  int get_iv_adjustment(MemNode* mem);
  // Can the preloop align the reference to position zero in the vector?
  bool ref_is_alignable(SWPointer& p);
  // Construct dependency graph.
  void dependence_graph();
  // Return a memory slice (node list) in predecessor order starting at "start"
  void mem_slice_preds(Node* start, Node* stop, GrowableArray<Node*> &preds);
  // Can s1 and s2 be in a pack with s1 immediately preceding s2 and  s1 aligned at "align"
  bool stmts_can_pack(Node* s1, Node* s2, int align);
  // Does s exist in a pack at position pos?
  bool exists_at(Node* s, uint pos);
  // Is s1 immediately before s2 in memory?
  bool are_adjacent_refs(Node* s1, Node* s2);
  // Are s1 and s2 similar?
  bool isomorphic(Node* s1, Node* s2);
  // Is there no data path from s1 to s2 or s2 to s1?
  bool independent(Node* s1, Node* s2);
  // Helper for independent
  bool independent_path(Node* shallow, Node* deep, uint dp=0);
  void set_alignment(Node* s1, Node* s2, int align);
  int data_size(Node* s);
  // Extend packset by following use->def and def->use links from pack members.
  void extend_packlist();
  // Extend the packset by visiting operand definitions of nodes in pack p
  bool follow_use_defs(Node_List* p);
  // Extend the packset by visiting uses of nodes in pack p
  bool follow_def_uses(Node_List* p);
  // Estimate the savings from executing s1 and s2 as a pack
  int est_savings(Node* s1, Node* s2);
  int adjacent_profit(Node* s1, Node* s2);
  int pack_cost(int ct);
  int unpack_cost(int ct);
  // Combine packs A and B with A.last == B.first into A.first..,A.last,B.second,..B.last
  void combine_packs();
  // Construct the map from nodes to packs.
  void construct_my_pack_map();
  // Remove packs that are not implemented or not profitable.
  void filter_packs();
  // Adjust the memory graph for the packed operations
  void schedule();
  // Remove "current" from its current position in the memory graph and insert
  // it after the appropriate insert points (lip or uip);
  void remove_and_insert(MemNode *current, MemNode *prev, MemNode *lip, Node *uip, Unique_Node_List &schd_before);
  // Within a store pack, schedule stores together by moving out the sandwiched memory ops according
  // to dependence info; and within a load pack, move loads down to the last executed load.
  void co_locate_pack(Node_List* p);
  // Convert packs into vector node operations
  void output();
  // Create a vector operand for the nodes in pack p for operand: in(opd_idx)
  Node* vector_opd(Node_List* p, int opd_idx);
  // Can code be generated for pack p?
  bool implemented(Node_List* p);
  // For pack p, are all operands and all uses (with in the block) vector?
  bool profitable(Node_List* p);
  // If a use of pack p is not a vector use, then replace the use with an extract operation.
  void insert_extracts(Node_List* p);
  // Is use->in(u_idx) a vector use?
  bool is_vector_use(Node* use, int u_idx);
  // Construct reverse postorder list of block members
  bool construct_bb();
  // Initialize per node info
  void initialize_bb();
  // Insert n into block after pos
  void bb_insert_after(Node* n, int pos);
  // Compute max depth for expressions from beginning of block
  void compute_max_depth();
  // Compute necessary vector element type for expressions
  void compute_vector_element_type();
  // Are s1 and s2 in a pack pair and ordered as s1,s2?
  bool in_packset(Node* s1, Node* s2);
  // Is s in pack p?
  Node_List* in_pack(Node* s, Node_List* p);
  // Remove the pack at position pos in the packset
  void remove_pack_at(int pos);
  // Return the node executed first in pack p.
  Node* executed_first(Node_List* p);
  // Return the node executed last in pack p.
  Node* executed_last(Node_List* p);
  static LoadNode::ControlDependency control_dependency(Node_List* p);
  // Alignment within a vector memory reference
  int memory_alignment(MemNode* s, int iv_adjust);
  // (Start, end] half-open range defining which operands are vector
  void vector_opd_range(Node* n, uint* start, uint* end);
  // Smallest type containing range of values
  const Type* container_type(Node* n);
  // Adjust pre-loop limit so that in main loop, a load/store reference
  // to align_to_ref will be a position zero in the vector.
  void align_initial_loop_index(MemNode* align_to_ref);
  // Find pre loop end from main loop.  Returns null if none.
  CountedLoopEndNode* get_pre_loop_end(CountedLoopNode *cl);
  // Is the use of d1 in u1 at the same operand position as d2 in u2?
  bool opnd_positions_match(Node* d1, Node* u1, Node* d2, Node* u2);
  void init();

  // print methods
  void print_packset();
  void print_pack(Node_List* p);
  void print_bb();
  void print_stmt(Node* s);
  char* blank(uint depth);
};



//------------------------------SWPointer---------------------------
// Information about an address for dependence checking and vector alignment
//
// We parse and represent pointers of the simple form:
//
//   pointer   = adr + offset + invar + scale * ConvI2L(iv)
//
// Where:
//
//   adr: the base address of an array (base = adr)
//        OR
//        some address to off-heap memory (base = TOP)
//
//   offset: a constant offset
//   invar:  a runtime variable, which is invariant during the loop
//   scale:  scaling factor
//   iv:     loop induction variable
//
// But more precisely, we parse the composite-long-int form:
//
//   pointer   = adr + long_offset + long_invar + long_scale * ConvI2L(int_offset + inv_invar + int_scale * iv)
//
//   pointer   = adr + long_offset + long_invar + long_scale * ConvI2L(int_index)
//   int_index =       int_offset  + int_invar  + int_scale  * iv
//
// However, for aliasing and adjacency checks (e.g. SWPointer::cmp()) we always use the simple form to make
// decisions. Hence, we must make sure to only create a "valid" SWPointer if the optimisations based on the
// simple form produce the same result as the compound-long-int form would. Intuitively, this depends on
// if the int_index overflows, but the precise conditions are given in SWPointer::is_safe_to_use_as_simple_form().
//
//   ConvI2L(int_index) = ConvI2L(int_offset  + int_invar  + int_scale  * iv)
//                      = Convi2L(int_offset) + ConvI2L(int_invar) + ConvI2L(int_scale) * ConvI2L(iv)
//
//   scale  = long_scale * ConvI2L(int_scale)
//   offset = long_offset + long_scale * ConvI2L(int_offset)
//   invar  = long_invar  + long_scale * ConvI2L(int_invar)
//
//   pointer   = adr + offset + invar + scale * ConvI2L(iv)
//
class SWPointer VALUE_OBJ_CLASS_SPEC {
 protected:
  MemNode*   _mem;     // My memory reference node
  SuperWord* _slp;     // SuperWord class

  // Components of the simple form:
  Node* _base;               // Base address of an array OR NULL if some off-heap memory.
  Node* _adr;                // Same as _base if an array pointer OR some off-heap memory pointer.
  jint  _scale;        // multiplier for iv (in bytes), 0 if no loop iv
  jint  _offset;       // constant offset (in bytes)
  Node* _invar;        // invariant offset (in bytes), NULL if none
  bool  _negate_invar; // if true then use: (0 - _invar)

  // The int_index components of the compound-long-int form. Used to decide if it is safe to use the
  // simple form rather than the compound-long-int form that was parsed.
  bool  _has_int_index_after_convI2L;
  int   _int_index_after_convI2L_offset;
  Node* _int_index_after_convI2L_invar;
  int   _int_index_after_convI2L_scale;

  PhaseIdealLoop* phase() { return _slp->phase(); }
  IdealLoopTree*  lpt()   { return _slp->lpt(); }
  PhiNode*        iv()    { return _slp->iv();  } // Induction var

  bool invariant(Node* n) {
    Node *n_c = phase()->get_ctrl(n);
    return !lpt()->is_member(phase()->get_loop(n_c));
  }

  // Match: k*iv + offset
  bool scaled_iv_plus_offset(Node* n);
  // Match: k*iv where k is a constant that's not zero
  bool scaled_iv(Node* n);
  // Match: offset is (k [+/- invariant])
  bool offset_plus_k(Node* n, bool negate = false);

  bool is_safe_to_use_as_simple_form(Node* base, Node* adr) const;

 public:
  enum CMP {
    Less          = 1,
    Greater       = 2,
    Equal         = 4,
    NotEqual      = (Less | Greater),
    NotComparable = (Less | Greater | Equal)
  };

  SWPointer(MemNode* mem, SuperWord* slp);
  // Following is used to create a temporary object during
  // the pattern match of an address expression.
  SWPointer(SWPointer* p);

  bool valid()  { return _adr != NULL; }
  bool has_iv() { return _scale != 0; }

  Node* base()            { return _base; }
  Node* adr()             { return _adr; }
  MemNode* mem()          { return _mem; }
  int   scale_in_bytes()  { return _scale; }
  Node* invar()           { return _invar; }
  bool  negate_invar()    { return _negate_invar; }
  int   offset_in_bytes() { return _offset; }
  int   memory_size()     { return _mem->memory_size(); }

  // Comparable?
  // We compute if and how two SWPointers can alias at runtime, i.e. if the two addressed regions of memory can
  // ever overlap. There are essentially 3 relevant return states:
  //  - NotComparable:  Synonymous to "unknown aliasing".
  //                    We have no information about how the two SWPointers can alias. They could overlap, refer
  //                    to another location in the same memory object, or point to a completely different object.
  //                    -> Memory edge required. Aliasing unlikely but possible.
  //
  //  - Less / Greater: Synonymous to "never aliasing".
  //                    The two SWPointers may point into the same memory object, but be non-aliasing (i.e. we
  //                    know both address regions inside the same memory object, but these regions are non-
  //                    overlapping), or the SWPointers point to entirely different objects.
  //                    -> No memory edge required. Aliasing impossible.
  //
  //  - Equal:          Synonymous to "overlap, or point to different memory objects".
  //                    The two SWPointers either overlap on the same memory object, or point to two different
  //                    memory objects.
  //                    -> Memory edge required. Aliasing likely.
  //
  // In a future refactoring, we can simplify to two states:
  //  - NeverAlias:     instead of Less / Greater
  //  - MayAlias:       instead of Equal / NotComparable
  //
  // Two SWPointer are "comparable" (Less / Greater / Equal), iff all of these conditions apply:
  //   1) Both are valid, i.e. expressible in the compound-long-int or simple form.
  //   2) The adr are identical, or both are array bases of different arrays.
  //   3) They have identical scale.
  //   4) They have identical invar.
  //   5) The difference in offsets is limited: abs(offset0 - offset1) < 2^31.
  int cmp(SWPointer& q) {
    if (valid() && q.valid() &&
        (_adr == q._adr || _base == _adr && q._base == q._adr) &&
        _scale == q._scale   &&
        _invar == q._invar   &&
        _negate_invar == q._negate_invar) {
      jlong difference = abs(java_subtract((jlong)_offset, (jlong)q._offset));
      jlong max_diff = (jlong)1 << 31;
      if (difference >= max_diff) {
        return NotComparable;
      }
      bool overlap = q._offset <   _offset +   memory_size() &&
                       _offset < q._offset + q.memory_size();
      return overlap ? Equal : (_offset < q._offset ? Less : Greater);
    } else {
      return NotComparable;
    }
  }

  bool not_equal(SWPointer& q)    { return not_equal(cmp(q)); }
  bool equal(SWPointer& q)        { return equal(cmp(q)); }
  bool comparable(SWPointer& q)   { return comparable(cmp(q)); }
  static bool not_equal(int cmp)  { return cmp <= NotEqual; }
  static bool equal(int cmp)      { return cmp == Equal; }
  static bool comparable(int cmp) { return cmp < NotComparable; }

  void print();

  static bool try_AddI_no_overflow(jint offset1, jint offset2, jint& result);
  static bool try_SubI_no_overflow(jint offset1, jint offset2, jint& result);
  static bool try_AddSubI_no_overflow(jint offset1, jint offset2, bool is_sub, jint& result);
  static bool try_LShiftI_no_overflow(jint offset1, int offset2, jint& result);
  static bool try_MulI_no_overflow(jint offset1, jint offset2, jint& result);

};

#endif // SHARE_VM_OPTO_SUPERWORD_HPP
