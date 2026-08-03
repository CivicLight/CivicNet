#include <pos_kernel.h>
#include <hash.h>
#include <crypto/sha256.h>
#include <cmath>
#include <chain.h>
#include <primitives/block.h>
#include <chainparams.h>

uint256 ComputeStakeModifier(const uint256& previousModifier,
                              const uint256& referenceHash,
                              uint32_t referenceHeight)
{
    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << previousModifier << referenceHash << referenceHeight;
    return ss.GetHash();
}

bool CheckStakeKernel(const uint256& stakeModifier,
                       uint32_t prevBlockTime,
                       uint32_t prevTxOffset,
                       uint32_t voutN,
                       uint32_t nTimeTx,
                       int64_t balance,
                       const arith_uint256& target)
{
    if (balance <= 0) return false;

    // kernel = Hash(stakeModifier, prevBlockTime, prevTxOffset, voutN, nTimeTx)
    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << stakeModifier << prevBlockTime << prevTxOffset << voutN << nTimeTx;
    uint256 kernelHash = ss.GetHash();

    // weight = sqrt(balance), scaled to keep integer precision reasonable
    // SECURITY FIX (High 5): arith_uint256 multiplication wraps silently
    // mod 2^256. With the initial target near 2^236, target*weight could
    // overflow once sqrt(balance) gets large enough, wrapping to a SMALL
    // value and making the anti-whale mechanism behave erratically right
    // in the balance range it's meant to protect. Detect the overflow via
    // division before multiplying, and clamp to the max representable
    // value instead of wrapping.
    double weight = std::sqrt((double)balance);
    uint64_t weightInt = (uint64_t)weight;
    arith_uint256 scaledTarget;
    if (weightInt <= 1) {
        scaledTarget = target;
    } else {
        arith_uint256 maxUint256 = ~arith_uint256(0);
        if (target > maxUint256 / weightInt) {
            scaledTarget = maxUint256; // would overflow -- clamp, don't wrap
        } else {
            scaledTarget = target * weightInt;
        }
    }

    return UintToArith256(kernelHash) < scaledTarget;
}


bool IsValidStakeTimestamp(uint32_t nTimeTx, uint32_t referenceTime)
{
    if (nTimeTx <= referenceTime) return false;
    if (nTimeTx % STAKE_TIMESTAMP_MASK != 0) return false;
    return true;
}

uint32_t NextValidStakeTimestamp(uint32_t referenceTime)
{
    uint32_t candidate = referenceTime + 1;
    uint32_t remainder = candidate % STAKE_TIMESTAMP_MASK;
    if (remainder != 0) {
        candidate += (STAKE_TIMESTAMP_MASK - remainder);
    }
    return candidate;
}


bool CanBeProofOfStake(int currentHeight, int lastPoSHeight)
{
    if (lastPoSHeight < 0) return true; // no PoS block yet, always allowed
    return (currentHeight - lastPoSHeight) > STAKE_MIN_POW_GAP;
}

bool IsStakeModifierBoundary(int height)
{
    if (height <= 0) return false;
    return (height % STAKE_MODIFIER_INTERVAL) == 0;
}

int GetStakeModifierReferenceHeight(int boundaryHeight)
{
    if (!IsStakeModifierBoundary(boundaryHeight)) return -1;
    int refHeight = boundaryHeight - STAKE_MODIFIER_REORG_OFFSET;
    if (refHeight < 0) return -1;
    return refHeight;
}

int AdjustPoSRatio(int currentRatioBps, int actualPoSBlocks, int targetPoSBlocks)
{
    int newRatioBps = currentRatioBps;

    if (actualPoSBlocks > targetPoSBlocks) {
        newRatioBps += STAKE_RATIO_STEP_BPS;
    } else if (actualPoSBlocks < targetPoSBlocks) {
        newRatioBps -= STAKE_RATIO_STEP_BPS;
    }

    if (newRatioBps < STAKE_RATIO_FLOOR_BPS) newRatioBps = STAKE_RATIO_FLOOR_BPS;
    if (newRatioBps > STAKE_RATIO_CEILING_BPS) newRatioBps = STAKE_RATIO_CEILING_BPS;

    return newRatioBps;
}

uint256 GetInitialStakeModifier(const uint256& genesisHash, uint32_t genesisTime)
{
    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << genesisHash << genesisTime;
    return ss.GetHash();
}

arith_uint256 GetNextStakeTarget(const arith_uint256& currentTarget,
                                  int64_t actualSpacing,
                                  int64_t targetSpacing,
                                  const arith_uint256& maxTarget)
{
    // SECURITY FIX (High 6): floating point is not guaranteed bit-identical
    // across compilers/libm/architectures in a consensus-critical path --
    // two honest nodes could disagree on block validity. Use exact
    // rational arithmetic instead (27/25 == 1.08 exactly).
    if (actualSpacing < targetSpacing * 25 / 27) actualSpacing = targetSpacing * 25 / 27;
    if (actualSpacing > targetSpacing * 27 / 25) actualSpacing = targetSpacing * 27 / 25;

    arith_uint256 newTarget = currentTarget;
    newTarget *= (uint64_t)actualSpacing;
    newTarget /= (uint64_t)targetSpacing;

    if (newTarget > maxTarget) newTarget = maxTarget;

    return newTarget;
}

uint32_t ComputeExpectedStakeTarget(const CBlockIndex* pindexPrev, const CBlockHeader& block)
{
    if (pindexPrev == nullptr) {
        return 0; // genesis; no PoS block is ever validated here
    }
    uint32_t baseTarget;
    if (pindexPrev->nStakeTarget == 0) {
        baseTarget = 0x1e0ffff0; // powLimit-equivalent compact bits (matches AddToBlockIndex's init case)
    } else {
        baseTarget = pindexPrev->nStakeTarget;
    }
    if (baseTarget != 0 && pindexPrev->nLastPoSHeight >= 0) {
        const CBlockIndex* prevPoSBlock = pindexPrev->GetAncestor(pindexPrev->nLastPoSHeight);
        if (prevPoSBlock != nullptr) {
            int64_t actualSpacing = (int64_t)block.nTime - (int64_t)prevPoSBlock->nTime;
            int ratioBps = pindexPrev->nPoSRatioBps > 0 ? pindexPrev->nPoSRatioBps : STAKE_RATIO_START_BPS;
            int64_t targetSpacing = (int64_t)Params().GetConsensus().nPowTargetSpacing * 10000LL / ratioBps;
            arith_uint256 currentTarget;
            currentTarget.SetCompact(baseTarget);
            arith_uint256 maxTarget;
            maxTarget.SetCompact(0x1e0ffff0);
            arith_uint256 newTarget = GetNextStakeTarget(currentTarget, actualSpacing, targetSpacing, maxTarget);
            return newTarget.GetCompact();
        }
    }
    return baseTarget;
}
