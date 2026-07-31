#include <pos_kernel.h>
#include <hash.h>
#include <crypto/sha256.h>
#include <cmath>

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
    double weight = std::sqrt((double)balance);
    arith_uint256 scaledTarget = target * (uint64_t)weight;

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
    if (actualSpacing < targetSpacing / 1.08) actualSpacing = (int64_t)(targetSpacing / 1.08);
    if (actualSpacing > targetSpacing * 1.08) actualSpacing = (int64_t)(targetSpacing * 1.08);

    arith_uint256 newTarget = currentTarget;
    newTarget *= (uint64_t)actualSpacing;
    newTarget /= (uint64_t)targetSpacing;

    if (newTarget > maxTarget) newTarget = maxTarget;

    return newTarget;
}
