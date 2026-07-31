#ifndef CIVICNET_POS_KERNEL_H
#define CIVICNET_POS_KERNEL_H

#include <uint256.h>
#include <arith_uint256.h>
#include <cstdint>

// Compute the next stake modifier deterministically from chain state.
// previousModifier: the modifier from the last update boundary
// referenceHash: hash of the block at (boundary_height - k)
// referenceHeight: that block's height
uint256 ComputeStakeModifier(const uint256& previousModifier,
                              const uint256& referenceHash,
                              uint32_t referenceHeight);

// Check whether a candidate kernel satisfies the staking condition:
// hash(kernel) < target * sqrt(balance)
bool CheckStakeKernel(const uint256& stakeModifier,
                       uint32_t prevBlockTime,
                       uint32_t prevTxOffset,
                       uint32_t voutN,
                       uint32_t nTimeTx,
                       int64_t balance,
                       const arith_uint256& target);

// nTimeTx (PoS "nonce" analog): must be strictly after the reference time,
// and a multiple of STAKE_TIMESTAMP_MASK to bound the search space
// (mirrors Peercoin's timestamp-mask approach).
static const uint32_t STAKE_TIMESTAMP_MASK = 8; // seconds

bool IsValidStakeTimestamp(uint32_t nTimeTx, uint32_t referenceTime);

// Rounds a candidate time up to the next valid mask-aligned timestamp
// strictly after referenceTime, for starting a staking search loop.
uint32_t NextValidStakeTimestamp(uint32_t referenceTime);

// Anti-clustering rule: minimum N PoW blocks required between two PoS
// blocks. N=2 is mathematically derived from the 30% PoS ceiling
// (max sustainable ratio = 1/(N+1); N=2 caps at ~33%, consistent with 30%).
static const int STAKE_MIN_POW_GAP = 2;

// currentHeight: height of the block being considered for PoS
// lastPoSHeight: height of the most recent PoS block (-1 if none yet)
bool CanBeProofOfStake(int currentHeight, int lastPoSHeight);

// Stake modifier update interval: every H blocks (~2 hours at 60s/block).
static const int STAKE_MODIFIER_INTERVAL = 120;

// Reorg-safety offset: reference block is k blocks before the boundary
// (~30 min), giving margin against short reorgs affecting the modifier.
static const int STAKE_MODIFIER_REORG_OFFSET = 30;

// True if this height is a modifier-update boundary (height % H == 0).
bool IsStakeModifierBoundary(int height);

// Given a boundary height, returns the reference height used to compute
// the new modifier (boundary - k). Returns -1 if height is not a boundary
// or if the reference height would be negative (too early in the chain).
int GetStakeModifierReferenceHeight(int boundaryHeight);

// Dynamic PoW/PoS ratio, expressed in basis points (10000 = 100%).
// Starts at 5% PoS, adjusts based on actual vs target participation each
// window, bounded between a 5% floor and 30% ceiling.
static const int STAKE_RATIO_START_BPS = 500;    // 5%
static const int STAKE_RATIO_FLOOR_BPS = 500;    // 5%
static const int STAKE_RATIO_CEILING_BPS = 3000; // 30%
static const int STAKE_RATIO_STEP_BPS = 150;     // 1.5% per window
static const int STAKE_RATIO_WINDOW_BLOCKS = 10080; // ~1 week at 60s/block

// Computes the next window's PoS ratio given the current ratio and how
// many PoS blocks were actually found vs targeted in the just-finished
// window. Over-delivery shifts toward more PoS, under-delivery shifts
// back toward PoW, always clamped to [floor, ceiling].
int AdjustPoSRatio(int currentRatioBps, int actualPoSBlocks, int targetPoSBlocks);

// Bootstrap value for the very first stake modifier (before the first
// H-block boundary), derived deterministically from the genesis block
// itself so testnet/mainnet automatically get distinct, independently
// verifiable initial modifiers -- no manually hardcoded constant needed.
uint256 GetInitialStakeModifier(const uint256& genesisHash, uint32_t genesisTime);

// PoS target adjustment: mirrors the proven PoW retarget pattern
// (+/-8% bound per adjustment) applied to actual vs target time between
// consecutive PoS blocks, rather than a novel untested mechanism.
arith_uint256 GetNextStakeTarget(const arith_uint256& currentTarget,
                                  int64_t actualSpacing,
                                  int64_t targetSpacing,
                                  const arith_uint256& maxTarget);

#endif // CIVICNET_POS_KERNEL_H
