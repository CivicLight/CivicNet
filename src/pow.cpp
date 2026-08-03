#include <pow.h>

#include <arith_uint256.h>
#include <chain.h>
#include <primitives/block.h>
#include <uint256.h>
#include <assert.h>

unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& params)
{
    assert(pindexLast != nullptr);
    unsigned int nProofOfWorkLimit = UintToArith256(params.powLimit).GetCompact();

    // Don't adjust difficulty if this is Block #0 (Genesis) or Block #1
    if (pindexLast->nHeight <= 1) {
        return nProofOfWorkLimit;
    }

    // Fork boundary: ONLY the single block whose own nTime crosses the
    // threshold while its parent is still pre-v2. Reset difficulty to the
    // easiest allowed value for that one block, so the network does not
    // stall waiting for the normal +/-8% per-block adjustment to catch up.
    if (pblock != nullptr &&
        pblock->nTime >= (uint32_t)CBlockHeader::CIVICLIGHT_V2_ACTIVATION_TIME &&
        pindexLast->GetBlockTime() < (int64_t)CBlockHeader::CIVICLIGHT_V2_ACTIVATION_TIME) {
        return nProofOfWorkLimit;
    }

    // SECURITY FIX (Medium 9): a single-block retarget window is
    // trivially influenced by timestamp manipulation and can't respond to
    // real hashrate changes at any useful speed -- this was the root cause
    // of the multi-hour stall at block 4794 (see CHANGELOG, July 23).
    // Average over a RETARGET_WINDOW-block span instead (50, matching our
    // coinbase maturity confirmation count), walking back as many blocks
    // as actually exist if the chain is younger than that.
    static const int RETARGET_WINDOW = 50;
    int nBlocksBack = RETARGET_WINDOW;
    const CBlockIndex* pindexFirst = pindexLast;
    for (int i = 0; i < nBlocksBack && pindexFirst->pprev != nullptr; i++) {
        pindexFirst = pindexFirst->pprev;
    }
    if (pindexFirst == pindexLast) return nProofOfWorkLimit; // not enough history yet
    int nActualBlocks = pindexLast->nHeight - pindexFirst->nHeight;

    // Compute the actual timespan versus the target for that many blocks
    int64_t nActualTimespan = pindexLast->GetBlockTime() - pindexFirst->GetBlockTime();
    int64_t nTargetSpacing = params.nPowTargetSpacing * (int64_t)nActualBlocks;

    // Clamp extreme swings so the adjustment doesn't jump too wildly (max +/-8%)
    // SECURITY FIX (High 6): floating point is not guaranteed bit-identical
    // across compilers/libm/architectures in a consensus-critical path.
    // Use exact rational arithmetic instead (27/25 == 1.08 exactly).
    if (nActualTimespan < nTargetSpacing * 25 / 27) nActualTimespan = nTargetSpacing * 25 / 27;
    if (nActualTimespan > nTargetSpacing * 27 / 25) nActualTimespan = nTargetSpacing * 27 / 25;

    arith_uint256 bnNew;
    bnNew.SetCompact(pindexLast->nBits);
    bnNew *= nActualTimespan;
    bnNew /= nTargetSpacing;

    if (bnNew > UintToArith256(params.powLimit)) {
        bnNew = UintToArith256(params.powLimit);
    }

    return bnNew.GetCompact();
}

unsigned int CalculateNextWorkRequired(const CBlockIndex* pindexLast, int64_t nFirstBlockTime, const Consensus::Params& params)
{
    if (params.fPowNoRetargeting)
        return pindexLast->nBits;

    return pindexLast->nBits;
}

bool CheckProofOfWork(uint256 hash, unsigned int nBits, const Consensus::Params& params)
{
    bool fNegative;
    bool fOverflow;
    arith_uint256 bnTarget;

    bnTarget.SetCompact(nBits, &fNegative, &fOverflow);

    // Reject if the target exceeds the network difficulty limit
    if (fNegative || bnTarget == 0 || fOverflow || bnTarget > UintToArith256(params.powLimit))
        return false;

    // Ensure the block hash is below the consensus difficulty target
    if (UintToArith256(hash) > bnTarget)
        return false;

    return true;
}
