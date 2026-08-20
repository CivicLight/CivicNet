#ifndef CIVICNET_POS_KERNEL_H
#define CIVICNET_POS_KERNEL_H

#include <uint256.h>
#include <arith_uint256.h>
#include <cstdint>
class CBlockIndex;
class CBlockHeader;

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

// Periodic PoS stake-target retarget window (post-POS_RETARGET_WINDOW_FIX_ACTIVATION_TIME).
// Matches pow.cpp's own RETARGET_WINDOW=50 windowed-averaging philosophy --
// retarget triggers every N blocks (PoW or PoS) instead of only on PoS
// events, so the target can no longer freeze indefinitely when PoS blocks
// stop occurring.
static const int POS_RETARGET_WINDOW = 50;

// Number of most-recent real PoS-to-PoS intervals to average when computing
// the retarget signal, instead of a single raw interval -- smooths one-off
// noise (a single unusually fast/slow PoS event) and avoids overreacting to
// normal Poisson variance in stake timing. Gracefully degrades: if fewer
// than this many real PoS blocks exist yet, uses however many are available
// (minimum 1, the still-open current gap since the last real PoS block).
static const int POS_RETARGET_AVERAGING_K = 5;
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
// SECURITY FIX (Critical 2): computes what nStakeTarget SHOULD be for the
// next block, using pindexPrev (available at header-validation time, before
// the new block's index entry exists) -- mirrors the target-only portion of
// the logic in validation.cpp's block-index-building step. Used to reject
// PoS blocks whose header nBits doesn't match this value, closing the
// unvalidated-nBits -> forged-chainwork -> arbitrary-reorg hole.
uint32_t ComputeExpectedStakeTarget(const CBlockIndex* pindexPrev, const CBlockHeader& block);

arith_uint256 GetNextStakeTarget(const arith_uint256& currentTarget,
                                  int64_t actualSpacing,
                                  int64_t targetSpacing,
                                  const arith_uint256& maxTarget);

// Walks back through real PoS block history (via nLastPoSHeight chains) to
// average up to POS_RETARGET_AVERAGING_K most-recent PoS-to-PoS intervals,
// including the still-open gap from the last real PoS block to currentTime.
// Returns the averaged spacing in seconds; samplesUsed is set to however
// many intervals were actually available (1..POS_RETARGET_AVERAGING_K).
int64_t GetAveragedPoSSpacing(const CBlockIndex* pindexTip, int64_t currentTime, int& samplesUsed);

// Tiered/graduated version of the PoS retarget clamp: instead of a single
// fixed +/-8% bound, the clamp magnitude scales with how far avgSpacing
// deviates from targetSpacing (mild deviation -> mild correction, severe
// deviation -> large correction), so a badly-stuck target (or a post-reset
// PoS flood) can self-correct in hours instead of days, while normal
// Poisson noise around the target still only sees the original +/-8% move.
// Floored at 1 (never 0 -- 0 means "uninitialized" elsewhere in this
// codebase and would otherwise be a spam-timestamp exploit vector).
arith_uint256 GetNextStakeTargetTiered(const arith_uint256& currentTarget,
                                        int64_t avgSpacing,
                                        int64_t targetSpacing,
                                        const arith_uint256& maxTarget);

#endif // CIVICNET_POS_KERNEL_H
