// Copyright (c) 2017 Google Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef LIBSPIRV_OPT_CFG_H_
#define LIBSPIRV_OPT_CFG_H_

#include "basic_block.h"

#include <algorithm>
#include <list>
#include <unordered_map>
#include <unordered_set>

namespace spvtools {
namespace ir {

class CFG {
 public:
  CFG(ir::Module* module);

  // Return the module described by this CFG.
  ir::Module* get_module() const { return module_; }

  // Return the list of predecesors for basic block with label |blkid|.
  // TODO(dnovillo): Move this to ir::BasicBlock.
  const std::vector<uint32_t>& preds(uint32_t blk_id) const {
    assert(label2preds_.count(blk_id));
    return label2preds_.at(blk_id);
  }

  // Return a pointer to the basic block instance corresponding to the label
  // |blk_id|.
  ir::BasicBlock* block(uint32_t blk_id) const { return id2block_.at(blk_id); }

  // Return the pseudo entry and exit blocks.
  const ir::BasicBlock* pseudo_entry_block() const {
    return &pseudo_entry_block_;
  }
  ir::BasicBlock* pseudo_entry_block() { return &pseudo_entry_block_; }

  const ir::BasicBlock* pseudo_exit_block() const {
    return &pseudo_exit_block_;
  }
  ir::BasicBlock* pseudo_exit_block() { return &pseudo_exit_block_; }

  // Return true if |block_ptr| is the pseudo-entry block.
  bool IsPseudoEntryBlock(ir::BasicBlock* block_ptr) const {
    return block_ptr == &pseudo_entry_block_;
  }

  // Return true if |block_ptr| is the pseudo-exit block.
  bool IsPseudoExitBlock(ir::BasicBlock* block_ptr) const {
    return block_ptr == &pseudo_exit_block_;
  }

  // Compute structured block order into |order| for |func| starting at |root|.
  // This order has the property that dominators come before all blocks they
  // dominate and merge blocks come after all blocks that are in the control
  // constructs of their header.
  void ComputeStructuredOrder(ir::Function* func, ir::BasicBlock* root,
                              std::list<ir::BasicBlock*>* order);

  // Applies |f| to the basic block in post order starting with |bb|.
  // Note that basic blocks that cannot be reached from |bb| node will not be
  // processed.
  void ForEachBlockInPostOrder(BasicBlock* bb,
                               const std::function<void(BasicBlock*)>& f);

  // Applies |f| to the basic block in reverse post order starting with |bb|.
  // Note that basic blocks that cannot be reached from |bb| node will not be
  // processed.
  void ForEachBlockInReversePostOrder(
      BasicBlock* bb, const std::function<void(BasicBlock*)>& f);

  // Registers |blk| as a basic block in the cfg, this also updates the
  // predecessor lists of each successor of |blk|.
  void RegisterBlock(ir::BasicBlock* blk) {
    uint32_t blk_id = blk->id();
    id2block_[blk_id] = blk;
    AddEdges(blk);
  }

  // Removes from the CFG any mapping for the basic block id |blk_id|.
  void ForgetBlock(const ir::BasicBlock* blk) {
    id2block_.erase(blk->id());
    label2preds_.erase(blk->id());
    RemoveSuccessorEdges(blk);
  }

  void RemoveEdge(uint32_t pred_blk_id, uint32_t succ_blk_id) {
    auto pred_it = label2preds_.find(succ_blk_id);
    if (pred_it == label2preds_.end()) return;
    auto& preds_list = pred_it->second;
    auto it = std::find(preds_list.begin(), preds_list.end(), pred_blk_id);
    if (it != preds_list.end()) preds_list.erase(it);
  }

  // Registers |blk| to all of its successors.
  void AddEdges(ir::BasicBlock* blk);

  // Registers the basic block id |pred_blk_id| as being a predecessor of the
  // basic block id |succ_blk_id|.
  void AddEdge(uint32_t pred_blk_id, uint32_t succ_blk_id) {
    label2preds_[succ_blk_id].push_back(pred_blk_id);
  }

  // Removes any edges that no longer exist from the predecessor mapping for
  // the basic block id |blk_id|.
  void RemoveNonExistingEdges(uint32_t blk_id);

  // Remove all edges that leave |bb|.
  void RemoveSuccessorEdges(const ir::BasicBlock* bb) {
    bb->ForEachSuccessorLabel(
        [bb, this](uint32_t succ_id) { RemoveEdge(bb->id(), succ_id); });
  }

  // Divides |block| into two basic blocks.  The first block will have the same
  // id as |block| and will become a preheader for the loop.  The other block
  // is a new block that will be the new loop header.
  //
  // Returns a pointer to the new loop header.
  BasicBlock* SplitLoopHeader(ir::BasicBlock* bb);

  std::unordered_set<BasicBlock*> FindReachableBlocks(BasicBlock* start);

 private:
  using cbb_ptr = const ir::BasicBlock*;

  // Compute structured successors for function |func|. A block's structured
  // successors are the blocks it branches to together with its declared merge
  // block and continue block if it has them. When order matters, the merge
  // block and continue block always appear first. This assures correct depth
  // first search in the presence of early returns and kills. If the successor
  // vector contain duplicates of the merge or continue blocks, they are safely
  // ignored by DFS.
  void ComputeStructuredSuccessors(ir::Function* func);

  // Computes the post-order traversal of the cfg starting at |bb| skipping
  // nodes in |seen|.  The order of the traversal is appended to |order|, and
  // all nodes in the traversal are added to |seen|.
  void ComputePostOrderTraversal(BasicBlock* bb,
                                 std::vector<BasicBlock*>* order,
                                 std::unordered_set<BasicBlock*>* seen);

  // Module for this CFG.
  ir::Module* module_;

  // Map from block to its structured successor blocks. See
  // ComputeStructuredSuccessors() for definition.
  std::unordered_map<const ir::BasicBlock*, std::vector<ir::BasicBlock*>>
      block2structured_succs_;

  // Extra block whose successors are all blocks with no predecessors
  // in function.
  ir::BasicBlock pseudo_entry_block_;

  // Augmented CFG Exit Block.
  ir::BasicBlock pseudo_exit_block_;

  // Map from block's label id to its predecessor blocks ids
  std::unordered_map<uint32_t, std::vector<uint32_t>> label2preds_;

  // Map from block's label id to block.
  std::unordered_map<uint32_t, ir::BasicBlock*> id2block_;
};

}  // namespace ir
}  // namespace spvtools

#endif  // LIBSPIRV_OPT_CFG_H_
