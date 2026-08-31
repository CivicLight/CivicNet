// Copyright (c) 2026 The CivicNet developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CIVICNET_TOKEN_TOKENDB_H
#define CIVICNET_TOKEN_TOKENDB_H

#include <dbwrapper.h>
#include <token/tokentx.h>
#include <primitives/transaction.h> // COutPoint
#include <script/script.h>
#include <amount.h>
#include <memory>
#include <vector>
#include <utility>
#include <map>
#include <set>
#include <tuple>

class CTokenRegistryEntry
{
public:
    std::string symbol;
    std::string name;
    uint8_t     nTokenType = 0;
    uint8_t     nDecimals = 0;
    uint64_t    nInitialSupply = 0;
    uint64_t    nCurrentSupply = 0;
    CAmount     nInitialReserveLocked = 0;
    CAmount     nCurrentReserveLocked = 0;
    CScript     issuerScriptPubKey;
    uint32_t    nFlags = 0;
    uint256     poeAnchorHash;
    uint256     issueTxid;
    uint32_t    nIssueHeight = 0;
    uint32_t    nVestingStartHeight = 0;
    uint32_t    nVestingDurationBlocks = 0;
    uint32_t    nVestingCliffBlocks = 0;
    uint64_t    nSupplyCap = 0;
    uint32_t    nMintAuthorityExpiryHeight = 0;
    uint8_t     nFeeMode = 0;
    uint16_t    nFeeBpsStart = 0;
    uint16_t    nFeeBpsEnd = 0;
    uint32_t    nFeeDecayDurationBlocks = 0;
    uint160     feeRecipientHash160;
    uint16_t    nFeeBpsBurn = 0;
    std::string metadataUri;
    uint256     metadataHash;
    bool        fMetadataImmutable = false;

    bool IsCapped() const { return (nFlags & TOKEN_FLAG_CAPPED) != 0; }
    bool HasTransferFee() const { return (nFlags & TOKEN_FLAG_TRANSFER_FEE) != 0; }
    bool HasMetadata() const { return (nFlags & TOKEN_FLAG_HAS_METADATA) != 0; }

    SERIALIZE_METHODS(CTokenRegistryEntry, obj)
    {
        READWRITE(obj.symbol, obj.name, obj.nTokenType, obj.nDecimals,
                  obj.nInitialSupply, obj.nCurrentSupply,
                  obj.nInitialReserveLocked, obj.nCurrentReserveLocked,
                  obj.issuerScriptPubKey, obj.nFlags, obj.poeAnchorHash,
                  obj.issueTxid, obj.nIssueHeight,
                  obj.nVestingStartHeight, obj.nVestingDurationBlocks, obj.nVestingCliffBlocks,
                  obj.nSupplyCap, obj.nMintAuthorityExpiryHeight,
                  obj.nFeeMode, obj.nFeeBpsStart, obj.nFeeBpsEnd, obj.nFeeDecayDurationBlocks,
                  obj.feeRecipientHash160, obj.nFeeBpsBurn,
                  obj.metadataUri, obj.metadataHash, obj.fMetadataImmutable);
    }
};

class CTokenCoin
{
public:
    uint256  tokenID;
    uint64_t nTokenAmount = 0;
    CScript  scriptPubKey;
    uint32_t nHeight = 0;

    SERIALIZE_METHODS(CTokenCoin, obj)
    {
        READWRITE(obj.tokenID, obj.nTokenAmount, obj.scriptPubKey, obj.nHeight);
    }
};

class CTokenBlockUndo
{
public:
    std::vector<uint256> newTokenIDs;
    std::vector<std::pair<uint256, CTokenRegistryEntry>> prevRegistryState;
    std::vector<std::pair<COutPoint, CTokenCoin>> spentTokenCoins;
    // Whole-list snapshot of the global issuance-heights volume tracker,
    // taken ONCE per block (before the first TX_ISSUE in that block
    // modifies it) -- simpler and safer for reorg than trying to
    // "un-prune" entries that were trimmed off the front during ApplyTokenTx.
    bool hasPrevIssuanceHeights = false;
    std::vector<uint32_t> prevIssuanceHeights;

    SERIALIZE_METHODS(CTokenBlockUndo, obj)
    {
        READWRITE(obj.newTokenIDs, obj.prevRegistryState, obj.spentTokenCoins,
                  obj.hasPrevIssuanceHeights, obj.prevIssuanceHeights);
    }
};

class CTokenDB
{
private:
    std::unique_ptr<CDBWrapper> m_db;

public:
    explicit CTokenDB(fs::path ldb_path, size_t nCacheSize, bool fMemory = false, bool fWipe = false);

    bool ReadTokenRegistry(const uint256& tokenID, CTokenRegistryEntry& entry) const;
    bool WriteTokenRegistry(const uint256& tokenID, const CTokenRegistryEntry& entry);
    bool EraseTokenRegistry(const uint256& tokenID);
    bool HaveTokenRegistry(const uint256& tokenID) const;

    bool ReadTokenCoin(const COutPoint& outpoint, CTokenCoin& coin) const;
    bool WriteTokenCoin(const COutPoint& outpoint, const CTokenCoin& coin);
    bool EraseTokenCoin(const COutPoint& outpoint);
    bool HaveTokenCoin(const COutPoint& outpoint) const;

    bool WriteTokenAddressIndex(const CScript& scriptPubKey, const uint256& tokenID, const COutPoint& outpoint);
    bool EraseTokenAddressIndex(const CScript& scriptPubKey, const uint256& tokenID, const COutPoint& outpoint);
    std::vector<COutPoint> GetTokenOutpointsForAddress(const CScript& scriptPubKey, const uint256& tokenID) const;

    bool ReadTokenBlockUndo(const uint256& blockHash, CTokenBlockUndo& undo) const;
    bool WriteTokenBlockUndo(const uint256& blockHash, const CTokenBlockUndo& undo);
    bool EraseTokenBlockUndo(const uint256& blockHash);

    // Global (not per-token) rolling list of TX_ISSUE block heights, used to
    // measure recent issuance volume for the anti-spam fee tier. Single
    // fixed-key entry -- see TokenVolumeKey in tokendb.cpp.
    bool ReadIssuanceHeights(std::vector<uint32_t>& heights) const;
    bool WriteIssuanceHeights(const std::vector<uint32_t>& heights);

    bool GetAllTokens(std::vector<std::pair<uint256, CTokenRegistryEntry>>& out) const;
    bool GetAllTokenCoins(std::vector<std::pair<COutPoint, CTokenCoin>>& out) const;
    std::vector<std::pair<uint256, COutPoint>> GetAllTokenOutpointsForAddress(const CScript& scriptPubKey) const;
};

/** In-memory write buffer over CTokenDB, mirroring how CCoinsViewCache
 *  defers real disk writes until the whole block is confirmed connectable.
 *  Now also buffers address-index ('a') ops the same way -- previously
 *  WriteTokenAddressIndex/EraseTokenAddressIndex were never called from
 *  anywhere, leaving the address index permanently empty; ApplyTokenTx now
 *  stages an add/erase via AddTokenCoin/SpendTokenCoin below and Flush()
 *  commits it alongside the registry/coin writes for the block. */
class CTokenViewCache
{
private:
    CTokenDB& m_db;
    std::map<uint256, CTokenRegistryEntry> m_registryCache;
    std::set<uint256> m_registryErases;
    // Issuance-heights volume tracker cache -- loaded lazily on first
    // access (mutable: GetIssuanceHeights() is const but populates this
    // cache on first call, same lazy-read-through pattern as elsewhere in
    // this class, just deferred instead of eager), written back on
    // Flush() only if modified this block.
    mutable std::vector<uint32_t> m_issuanceHeightsCache;
    mutable bool m_issuanceHeightsLoaded = false;
    bool m_issuanceHeightsDirty = false;
    std::map<COutPoint, CTokenCoin> m_coinCacheAdds;
    std::set<COutPoint> m_coinCacheErases;
    // Address-index staging, keyed by outpoint (unique per coin). Final
    // op wins if a coin is added then spent within the same block. Value:
    // {scriptPubKey, tokenID, isAdd} -- isAdd=true means write the index
    // entry on Flush(), false means erase it.
    std::map<COutPoint, std::tuple<CScript, uint256, bool>> m_addressIndexOps;

public:
    explicit CTokenViewCache(CTokenDB& db) : m_db(db) {}

    //! Returns the current global issuance-heights list (loads from disk
    //! once, lazily, then serves from cache for the rest of the block).
    const std::vector<uint32_t>& GetIssuanceHeights() const
    {
        if (!m_issuanceHeightsLoaded) {
            m_db.ReadIssuanceHeights(m_issuanceHeightsCache); // ok if empty/missing
            m_issuanceHeightsLoaded = true;
        }
        return m_issuanceHeightsCache;
    }
    void SetIssuanceHeights(const std::vector<uint32_t>& heights)
    {
        m_issuanceHeightsCache = heights;
        m_issuanceHeightsLoaded = true;
        m_issuanceHeightsDirty = true;
    }

    bool HaveTokenRegistry(const uint256& tokenID) const
    {
        if (m_registryErases.count(tokenID)) return false;
        if (m_registryCache.count(tokenID)) return true;
        return m_db.HaveTokenRegistry(tokenID);
    }
    bool GetTokenRegistry(const uint256& tokenID, CTokenRegistryEntry& entry) const
    {
        if (m_registryErases.count(tokenID)) return false;
        auto it = m_registryCache.find(tokenID);
        if (it != m_registryCache.end()) { entry = it->second; return true; }
        return m_db.ReadTokenRegistry(tokenID, entry);
    }
    void SetTokenRegistry(const uint256& tokenID, const CTokenRegistryEntry& entry)
    {
        m_registryErases.erase(tokenID);
        m_registryCache[tokenID] = entry;
    }
    void EraseTokenRegistry(const uint256& tokenID)
    {
        m_registryCache.erase(tokenID);
        m_registryErases.insert(tokenID);
    }

    bool HaveTokenCoin(const COutPoint& outpoint) const
    {
        if (m_coinCacheErases.count(outpoint)) return false;
        if (m_coinCacheAdds.count(outpoint)) return true;
        return m_db.HaveTokenCoin(outpoint);
    }
    bool GetTokenCoin(const COutPoint& outpoint, CTokenCoin& coin) const
    {
        if (m_coinCacheErases.count(outpoint)) return false;
        auto it = m_coinCacheAdds.find(outpoint);
        if (it != m_coinCacheAdds.end()) { coin = it->second; return true; }
        return m_db.ReadTokenCoin(outpoint, coin);
    }
    //! Records a new token-colored UTXO AND stages its address-index entry.
    void AddTokenCoin(const COutPoint& outpoint, const CTokenCoin& coin)
    {
        m_coinCacheErases.erase(outpoint);
        m_coinCacheAdds[outpoint] = coin;
        m_addressIndexOps[outpoint] = std::make_tuple(coin.scriptPubKey, coin.tokenID, true);
    }
    //! Marks a token-colored UTXO as spent AND stages removal of its
    //! address-index entry. Needs `coin` (the data being spent) to know
    //! which scriptPubKey/tokenID to erase from the index.
    void SpendTokenCoin(const COutPoint& outpoint, const CTokenCoin& coin)
    {
        m_coinCacheAdds.erase(outpoint);
        m_coinCacheErases.insert(outpoint);
        m_addressIndexOps[outpoint] = std::make_tuple(coin.scriptPubKey, coin.tokenID, false);
    }

    //! Commits every buffered change to the underlying CTokenDB. Only call
    //! after the ENTIRE block is confirmed connectable.
    bool Flush()
    {
        for (const auto& kv : m_registryCache) {
            if (!m_db.WriteTokenRegistry(kv.first, kv.second)) return false;
        }
        for (const uint256& tokenID : m_registryErases) {
            if (!m_db.EraseTokenRegistry(tokenID)) return false;
        }
        for (const auto& kv : m_coinCacheAdds) {
            if (!m_db.WriteTokenCoin(kv.first, kv.second)) return false;
        }
        for (const auto& op : m_coinCacheErases) {
            if (!m_db.EraseTokenCoin(op)) return false;
        }
        for (const auto& kv : m_addressIndexOps) {
            const CScript& scriptPubKey = std::get<0>(kv.second);
            const uint256& tokenID = std::get<1>(kv.second);
            bool isAdd = std::get<2>(kv.second);
            if (isAdd) {
                if (!m_db.WriteTokenAddressIndex(scriptPubKey, tokenID, kv.first)) return false;
            } else {
                if (!m_db.EraseTokenAddressIndex(scriptPubKey, tokenID, kv.first)) return false;
            }
        }
        m_registryCache.clear();
        m_registryErases.clear();
        m_coinCacheAdds.clear();
        m_coinCacheErases.clear();
        m_addressIndexOps.clear();
        if (m_issuanceHeightsDirty) {
            if (!m_db.WriteIssuanceHeights(m_issuanceHeightsCache)) return false;
            m_issuanceHeightsDirty = false;
        }
        return true;
    }
};

#endif // CIVICNET_TOKEN_TOKENDB_H
