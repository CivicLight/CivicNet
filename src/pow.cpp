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
    int nActualBlocks = 0;

    // POW_RETARGET_FIX_V3: find the nearest real PoW block for the window's
    // END endpoint FIRST, before the window walk runs -- and walk the window
    // back from THAT block, not from pindexLast. Finding it after the window
    // walk (as before) meant that same PoW block got double-counted: once as
    // the window's first step, and again as the timespan's end endpoint --
    // silently shrinking the window to 49 real intervals while nTargetSpacing
    // was still computed for 50. Root-caused via community report (thanks,
    // NitroPool). SEPARATE activation gate from the V1/V2 fixes below, per
    // the same non-retroactivity lesson (see CHANGELOG / bnNew-base V1 incident).
    bool fTimespanEndFixActive = (pblock != nullptr &&
        CBlockHeader::POW_RETARGET_FIX_V3_ACTIVATION_TIME != 0 &&
        pblock->nTime >= (uint32_t)CBlockHeader::POW_RETARGET_FIX_V3_ACTIVATION_TIME);
    const CBlockIndex* pindexTipPoW = pindexLast;
    if (fTimespanEndFixActive) {
        while (pindexTipPoW != nullptr) {
            bool fIsPoS = (pindexTipPoW->nTime >= CBlockHeader::POS_ACTIVATION_TIME) &&
                          (pindexTipPoW->nVersion & CBlockHeader::VERSIONBITS_POS_FLAG) != 0;
            if (!fIsPoS) break;
            pindexTipPoW = pindexTipPoW->pprev;
        }
        if (pindexTipPoW == nullptr) return nProofOfWorkLimit;
    }
    const CBlockIndex* pWindowStart = fTimespanEndFixActive ? pindexTipPoW : pindexLast;
    pindexFirst = pWindowStart;

    // POW_RETARGET_FIX: PoS blocks are found via staking, not hashing, so
    // their fast/slow find-times don't reflect real PoW hashrate. Including
    // them in the retarget window causes the difficulty to overshoot.
    // Once active, walk back counting ONLY PoW blocks toward the window --
    // PoS blocks are skipped entirely (don't count as a step, don't count
    // toward nActualBlocks).
    bool fPowRetargetFixActive = (pblock != nullptr &&
        CBlockHeader::POW_RETARGET_FIX_ACTIVATION_TIME != 0 &&
        pblock->nTime >= (uint32_t)CBlockHeader::POW_RETARGET_FIX_ACTIVATION_TIME);

    if (fPowRetargetFixActive) {
        const CBlockIndex* p = pWindowStart;
        while (p->pprev != nullptr && nActualBlocks < nBlocksBack) {
            p = p->pprev;
            bool fIsPoS = (p->nTime >= CBlockHeader::POS_ACTIVATION_TIME) &&
                          (p->nVersion & CBlockHeader::VERSIONBITS_POS_FLAG) != 0;
            if (!fIsPoS) {
                nActualBlocks++;
            }
            pindexFirst = p;
        }
    } else {
        for (int i = 0; i < nBlocksBack && pindexFirst->pprev != nullptr; i++) {
            pindexFirst = pindexFirst->pprev;
        }
        nActualBlocks = pWindowStart->nHeight - pindexFirst->nHeight;
    }

    if (pindexFirst == pWindowStart) return nProofOfWorkLimit; // not enough history yet
    if (nActualBlocks <= 0) return nProofOfWorkLimit; // no PoW blocks found in lookback window

    // Compute the actual timespan versus the target for that many blocks
    int64_t nActualTimespan = pindexTipPoW->GetBlockTime() - pindexFirst->GetBlockTime();
    int64_t nTargetSpacing = params.nPowTargetSpacing * (int64_t)nActualBlocks;

    // Clamp extreme swings so the adjustment doesn't jump too wildly (max +/-8%)
    // SECURITY FIX (High 6): floating point is not guaranteed bit-identical
    // across compilers/libm/architectures in a consensus-critical path.
    // Use exact rational arithmetic instead (27/25 == 1.08 exactly).
    if (nActualTimespan < nTargetSpacing * 25 / 27) nActualTimespan = nTargetSpacing * 25 / 27;
    if (nActualTimespan > nTargetSpacing * 27 / 25) nActualTimespan = nTargetSpacing * 27 / 25;

    // POW_RETARGET_FIX (part 2): pindexLast may be a PoS block, whose nBits
    // is not a real computed PoW difficulty (PoS blocks inherit/copy nBits
    // from an earlier PoW block instead of computing one). Basing the new
    // difficulty on that borrowed value causes it to snap back to stale
    // data instead of progressing from the properly-averaged window.
    // Walk back (including pindexLast itself) to the nearest true PoW
    // block and use ITS nBits as the base instead.
    // SEPARATE activation gate from fPowRetargetFixActive -- this part of
    // the fix must NOT apply retroactively to blocks mined before its own
    // activation, or nodes validating fresh vs nodes that already had the
    // block connected will disagree (this caused a real incident -- see
    // CHANGELOG). Only takes effect once POW_RETARGET_FIX_V2_ACTIVATION_TIME
    // is reached.
    bool fBnNewBaseFixActive = (pblock != nullptr &&
        CBlockHeader::POW_RETARGET_FIX_V2_ACTIVATION_TIME != 0 &&
        pblock->nTime >= (uint32_t)CBlockHeader::POW_RETARGET_FIX_V2_ACTIVATION_TIME);
    const CBlockIndex* pindexLastPoW = pindexLast;
    if (fBnNewBaseFixActive) {
        while (pindexLastPoW != nullptr) {
            bool fIsPoS = (pindexLastPoW->nTime >= CBlockHeader::POS_ACTIVATION_TIME) &&
                          (pindexLastPoW->nVersion & CBlockHeader::VERSIONBITS_POS_FLAG) != 0;
            if (!fIsPoS) break;
            pindexLastPoW = pindexLastPoW->pprev;
        }
        if (pindexLastPoW == nullptr) return nProofOfWorkLimit;
    }

    arith_uint256 bnNew;
    bnNew.SetCompact(pindexLastPoW->nBits);
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
