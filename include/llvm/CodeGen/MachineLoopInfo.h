//===- llvm/CodeGen/MachineLoopInfo.h - Natural Loop Calculator -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the MachineLoopInfo class that is used to identify natural 
// loops and determine the loop depth of various nodes of the CFG.  Note that
// natural loops may actually be several loops that share the same header node.
//
// This analysis calculates the nesting structure of loops in a function.  For
// each natural loop identified, this analysis identifies natural loops
// contained entirely within the loop and the basic blocks the make up the loop.
//
// It can calculate on the fly various bits of information, for example:
//
//  * whether there is a preheader for the loop
//  * the number of back edges to the header
//  * whether or not a particular block branches out of the loop
//  * the successor blocks of the loop
//  * the loop depth
//  * the trip count
//  * etc...
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINE_LOOP_INFO_H
#define LLVM_CODEGEN_MACHINE_LOOP_INFO_H

#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/Analysis/LoopInfo.h"

namespace llvm {

class MachineLoop;

// Provide overrides for Loop methods that don't make sense for machine loops.
template<> inline
PHINode *
LoopBase<MachineBasicBlock, MachineLoop>::getCanonicalInductionVariable() const {
  assert(0 && "getCanonicalInductionVariable not supported for machine loops!");
  return 0;
}

template<> inline Instruction*
LoopBase<MachineBasicBlock,
         MachineLoop>::getCanonicalInductionVariableIncrement() const {
  assert(0 &&
     "getCanonicalInductionVariableIncrement not supported for machine loops!");
  return 0;
}

template<>
inline bool
LoopBase<MachineBasicBlock, MachineLoop>::isLoopInvariant(Value *V) const {
  assert(0 && "isLoopInvariant not supported for machine loops!");
  return false;
}

template<>
inline Value *
LoopBase<MachineBasicBlock, MachineLoop>::getTripCount() const {
  assert(0 && "getTripCount not supported for machine loops!");
  return 0;
}

template<>
inline bool
LoopBase<MachineBasicBlock, MachineLoop>::isLCSSAForm() const {
  assert(0 && "isLCSSAForm not supported for machine loops");
  return false;
}

class MachineLoop : public LoopBase<MachineBasicBlock, MachineLoop> {
public:
  MachineLoop();
private:
  friend class LoopInfoBase<MachineBasicBlock, MachineLoop>;
  explicit MachineLoop(MachineBasicBlock *MBB)
    : LoopBase<MachineBasicBlock, MachineLoop>(MBB) {}
};

class MachineLoopInfo : public MachineFunctionPass {
  LoopInfoBase<MachineBasicBlock, MachineLoop> LI;
  friend class LoopBase<MachineBasicBlock, MachineLoop>;

  void operator=(const MachineLoopInfo &);  // do not implement
  MachineLoopInfo(const MachineLoopInfo &); // do not implement

  LoopInfoBase<MachineBasicBlock, MachineLoop>& getBase() { return LI; }

public:
  static char ID; // Pass identification, replacement for typeid

  MachineLoopInfo() : MachineFunctionPass(&ID) {}

  /// iterator/begin/end - The interface to the top-level loops in the current
  /// function.
  ///
  typedef LoopInfoBase<MachineBasicBlock, MachineLoop>::iterator iterator;
  inline iterator begin() const { return LI.begin(); }
  inline iterator end() const { return LI.end(); }
  bool empty() const { return LI.empty(); }

  /// getLoopFor - Return the inner most loop that BB lives in.  If a basic
  /// block is in no loop (for example the entry node), null is returned.
  ///
  inline MachineLoop *getLoopFor(const MachineBasicBlock *BB) const {
    return LI.getLoopFor(BB);
  }

  /// operator[] - same as getLoopFor...
  ///
  inline const MachineLoop *operator[](const MachineBasicBlock *BB) const {
    return LI.getLoopFor(BB);
  }

  /// getLoopDepth - Return the loop nesting level of the specified block...
  ///
  inline unsigned getLoopDepth(const MachineBasicBlock *BB) const {
    return LI.getLoopDepth(BB);
  }

  // isLoopHeader - True if the block is a loop header node
  inline bool isLoopHeader(MachineBasicBlock *BB) const {
    return LI.isLoopHeader(BB);
  }

  /// runOnFunction - Calculate the natural loop information.
  ///
  virtual bool runOnMachineFunction(MachineFunction &F);

  virtual void releaseMemory() { LI.releaseMemory(); }

  virtual void getAnalysisUsage(AnalysisUsage &AU) const;

  /// removeLoop - This removes the specified top-level loop from this loop info
  /// object.  The loop is not deleted, as it will presumably be inserted into
  /// another loop.
  inline MachineLoop *removeLoop(iterator I) { return LI.removeLoop(I); }

  /// changeLoopFor - Change the top-level loop that contains BB to the
  /// specified loop.  This should be used by transformations that restructure
  /// the loop hierarchy tree.
  inline void changeLoopFor(MachineBasicBlock *BB, MachineLoop *L) {
    LI.changeLoopFor(BB, L);
  }

  /// changeTopLevelLoop - Replace the specified loop in the top-level loops
  /// list with the indicated loop.
  inline void changeTopLevelLoop(MachineLoop *OldLoop, MachineLoop *NewLoop) {
    LI.changeTopLevelLoop(OldLoop, NewLoop);
  }

  /// addTopLevelLoop - This adds the specified loop to the collection of
  /// top-level loops.
  inline void addTopLevelLoop(MachineLoop *New) {
    LI.addTopLevelLoop(New);
  }

  /// removeBlock - This method completely removes BB from all data structures,
  /// including all of the Loop objects it is nested in and our mapping from
  /// MachineBasicBlocks to loops.
  void removeBlock(MachineBasicBlock *BB) {
    LI.removeBlock(BB);
  }
};


// Allow clients to walk the list of nested loops...
template <> struct GraphTraits<const MachineLoop*> {
  typedef const MachineLoop NodeType;
  typedef MachineLoopInfo::iterator ChildIteratorType;

  static NodeType *getEntryNode(const MachineLoop *L) { return L; }
  static inline ChildIteratorType child_begin(NodeType *N) {
    return N->begin();
  }
  static inline ChildIteratorType child_end(NodeType *N) {
    return N->end();
  }
};

template <> struct GraphTraits<MachineLoop*> {
  typedef MachineLoop NodeType;
  typedef MachineLoopInfo::iterator ChildIteratorType;

  static NodeType *getEntryNode(MachineLoop *L) { return L; }
  static inline ChildIteratorType child_begin(NodeType *N) {
    return N->begin();
  }
  static inline ChildIteratorType child_end(NodeType *N) {
    return N->end();
  }
};

} // End llvm namespace

#endif
