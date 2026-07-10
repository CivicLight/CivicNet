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

    // Jangan ubah kesulitan jika ini adalah Blok #0 (Genesis) atau Blok #1
    if (pindexLast->nHeight <= 1) {
        return nProofOfWorkLimit;
    }

    const CBlockIndex* pindexFirst = pindexLast->pprev;
    if (pindexFirst == nullptr) return nProofOfWorkLimit;

    // Hitung jarak waktu pengerjaan blok nyata dibanding target (1 menit / 60 detik)
    int64_t nActualTimespan = pindexLast->GetBlockTime() - pindexFirst->GetBlockTime();
    int64_t nTargetSpacing = params.nPowTargetSpacing; // 60 detik

    // Batasi perubahan ekstrem agar tidak melompat terlalu liar (Maksimal naik/turun 8%)
    if (nActualTimespan < nTargetSpacing / 1.08) nActualTimespan = nTargetSpacing / 1.08;
    if (nActualTimespan > nTargetSpacing * 1.08) nActualTimespan = nTargetSpacing * 1.08;

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

    // Baris pengecekan batas maksimal target kesulitan
    if (fNegative || bnTarget == 0 || fOverflow || bnTarget > UintToArith256(params.powLimit))
        return false;

    // Memastikan hash blok berada di bawah target kesulitan konsensus
    if (UintToArith256(hash) > bnTarget)
        return false;

    return true;
}
