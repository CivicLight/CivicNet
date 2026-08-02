// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_PRIMITIVES_BLOCK_H
#define BITCOIN_PRIMITIVES_BLOCK_H

#include <primitives/transaction.h>
#include <serialize.h>
#include <uint256.h>
#include <mweb/mweb_models.h>
#include <crypto/civiclight_hash.h>

/** Nodes collect new transactions into a block, hash them into a hash tree,
 * and scan through nonce values to make the block's hash satisfy proof-of-work
 * requirements.  When they solve the proof-of-work, they broadcast the block
 * to everyone and the block is added to the block chain.  The first transaction
 * in the block is a special one that creates a new coin owned by the creator
 * of the block.
 */
class CBlockHeader
{
public:
    // header
    int32_t nVersion;
    uint256 hashPrevBlock;
    uint256 hashMerkleRoot;
    uint32_t nTime;
    uint32_t nBits;
    uint32_t nNonce;

    CBlockHeader()
    {
        SetNull();
    }

    SERIALIZE_METHODS(CBlockHeader, obj) { READWRITE(obj.nVersion, obj.hashPrevBlock, obj.hashMerkleRoot, obj.nTime, obj.nBits, obj.nNonce); }

    void SetNull()
    {
        nVersion = 0;
        hashPrevBlock.SetNull();
        hashMerkleRoot.SetNull();
        nTime = 0;
        nBits = 0;
        nNonce = 0;
    }

    bool IsNull() const
    {
        return (nBits == 0);
    }

            uint256 GetHash() const
    {
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << nVersion << hashPrevBlock << hashMerkleRoot << nTime << nBits << nNonce;
        return ss.GetHash();
    }

    // Fork activation: blocks with nTime before this use the original
    // algorithm; blocks at/after this use the ASIC-resistant civiclight v2.
    static const uint32_t CIVICLIGHT_V2_ACTIVATION_TIME = 1784797200;

    // Hybrid PoW+PoS: bit 16 of nVersion marks a block as Proof-of-Stake.
    // This flag alone is not trusted by consensus -- it must be validated
    // against a matching coinstake transaction structure (vtx[1]).
    static const int32_t VERSIONBITS_POS_FLAG = (1 << 16);
    // PoS activation gate: before this time, IsProofOfStake() always
    // returns false regardless of the version bit -- any block claiming
    // to be PoS before activation is therefore treated as an ordinary PoW
    // block by every downstream check (CheckBlock, ConnectBlock,
    // ReadBlockFromDisk's PoW-check skip, etc.) and will be rejected
    // unless it also happens to satisfy real proof-of-work, which a real
    // PoS-kernel-derived block will not. This is the single chokepoint
    // gating hybrid PoW+PoS activation across the whole codebase.
    static const uint32_t POS_ACTIVATION_TIME = 1785834000; // 2026-08-04 09:00:00 UTC

    bool IsProofOfStake() const {
        return nTime >= POS_ACTIVATION_TIME && (nVersion & VERSIONBITS_POS_FLAG) != 0;
    }

    uint256 GetPoWHash() const
    {
        uint256 thash;
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << nVersion << hashPrevBlock << hashMerkleRoot << nTime << nBits << nNonce;
        uint256 raw_hash = ss.GetHash();
        if (nTime >= CIVICLIGHT_V2_ACTIVATION_TIME) {
            civiclight_hash_v2(&raw_hash, 32, &thash);
        } else {
            civiclight_hash_v1(&raw_hash, 32, &thash);
        }
        return thash;
    }


    int64_t GetBlockTime() const
    {
        return (int64_t)nTime;
    }
};


class CBlock : public CBlockHeader
{
public:

    // network and disk
    std::vector<CTransactionRef> vtx;

    // memory only
    mutable bool fChecked;

    MWEB::Block mweb_block;

    CBlock()
    {
        SetNull();
    }

    CBlock(const CBlockHeader &header)
    {
        SetNull();
        *(static_cast<CBlockHeader*>(this)) = header;
    }

    SERIALIZE_METHODS(CBlock, obj)
    {
        READWRITEAS(CBlockHeader, obj);
        READWRITE(obj.vtx);
        if (!(s.GetVersion() & SERIALIZE_NO_MWEB)) {
            if (obj.vtx.size() >= 2 && obj.vtx.back()->IsHogEx()) {
                READWRITE(obj.mweb_block);
            }
        }
    }

    void SetNull()
    {
        CBlockHeader::SetNull();
        vtx.clear();
        fChecked = false;
        mweb_block.SetNull();
    }

    CBlockHeader GetBlockHeader() const
    {
        CBlockHeader block;
        block.nVersion       = nVersion;
        block.hashPrevBlock  = hashPrevBlock;
        block.hashMerkleRoot = hashMerkleRoot;
        block.nTime          = nTime;
        block.nBits          = nBits;
        block.nNonce         = nNonce;
        return block;
    }

    std::string ToString() const;

    // Returns the hogex (integrating) transaction, if it exists.
    CTransactionRef GetHogEx() const noexcept {
        return nullptr;
    }
};

/** Describes a place in the block chain to another node such that if the
 * other node doesn't have the same branch, it can find a recent common trunk.
 * The further back it is, the further before the fork it may be.
 */
struct CBlockLocator
{
    std::vector<uint256> vHave;

    CBlockLocator() {}

    explicit CBlockLocator(const std::vector<uint256>& vHaveIn) : vHave(vHaveIn) {}

    SERIALIZE_METHODS(CBlockLocator, obj)
    {
        int nVersion = s.GetVersion();
        if (!(s.GetType() & SER_GETHASH))
            READWRITE(nVersion);
        READWRITE(obj.vHave);
    }

    void SetNull()
    {
        vHave.clear();
    }

    bool IsNull() const
    {
        return vHave.empty();
    }
};

#endif // BITCOIN_PRIMITIVES_BLOCK_H
