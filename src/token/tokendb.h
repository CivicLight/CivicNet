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

/** Full metadata "registry card" for one issued token, keyed by tokenID
 *  (== the txid of its TX_ISSUE). Analogous in role to how `chainstate`
 *  tracks the CIVIC UTXO set, but for token-level bookkeeping. */
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
    CScript     issuerScriptPubKey;      // checked against for supply top-up authority
    uint32_t    nFlags = 0;
    uint256     poeAnchorHash;
    uint256     issueTxid;               // == tokenID itself, kept for convenience
    uint32_t    nIssueHeight = 0;
    uint32_t    nVestingStartHeight = 0;
    uint32_t    nVestingDurationBlocks = 0;
    uint32_t    nVestingCliffBlocks = 0;

    SERIALIZE_METHODS(CTokenRegistryEntry, obj)
    {
        READWRITE(obj.symbol, obj.name, obj.nTokenType, obj.nDecimals,
                  obj.nInitialSupply, obj.nCurrentSupply,
                  obj.nInitialReserveLocked, obj.nCurrentReserveLocked,
                  obj.issuerScriptPubKey, obj.nFlags, obj.poeAnchorHash,
                  obj.issueTxid, obj.nIssueHeight,
                  obj.nVestingStartHeight, obj.nVestingDurationBlocks, obj.nVestingCliffBlocks);
    }
};

/** One token-denominated UTXO (a "colored" output). Mirrors the shape of
 *  `Coin` (coins.h) but for token balances rather than CIVIC. */
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

/** Per-block undo data for token state, so DisconnectBlock can roll back
 *  every token-side effect of a block cleanly on reorg. */
class CTokenBlockUndo
{
public:
    std::vector<uint256> newTokenIDs;
    std::vector<std::pair<uint256, CTokenRegistryEntry>> prevRegistryState;
    std::vector<std::pair<COutPoint, CTokenCoin>> spentTokenCoins;

    SERIALIZE_METHODS(CTokenBlockUndo, obj)
    {
        READWRITE(obj.newTokenIDs, obj.prevRegistryState, obj.spentTokenCoins);
    }
};

/** LevelDB-backed storage for the Hybrid Value Layer -- a separate database
 *  (tokendb/), analogous in role to chainstate/ for CIVIC's own UTXO set,
 *  kept as its own instance so it can be reindexed/rolled back independently
 *  of the main chainstate if a bug is ever found in this layer. */
class CTokenDB
{
private:
    std::unique_ptr<CDBWrapper> m_db;

public:
    explicit CTokenDB(fs::path ldb_path, size_t nCacheSize, bool fMemory = false, bool fWipe = false);

    // --- Registry ('t') ---
    bool ReadTokenRegistry(const uint256& tokenID, CTokenRegistryEntry& entry) const;
    bool WriteTokenRegistry(const uint256& tokenID, const CTokenRegistryEntry& entry);
    bool EraseTokenRegistry(const uint256& tokenID);
    bool HaveTokenRegistry(const uint256& tokenID) const;

    // --- Token-UTXO set ('u') ---
    bool ReadTokenCoin(const COutPoint& outpoint, CTokenCoin& coin) const;
    bool WriteTokenCoin(const COutPoint& outpoint, const CTokenCoin& coin);
    bool EraseTokenCoin(const COutPoint& outpoint);
    bool HaveTokenCoin(const COutPoint& outpoint) const;

    // --- Address index ('a'), display/wallet convenience only -- never
    //     consulted for consensus validation, safe to rebuild via reindex. ---
    bool WriteTokenAddressIndex(const CScript& scriptPubKey, const uint256& tokenID, const COutPoint& outpoint);
    bool EraseTokenAddressIndex(const CScript& scriptPubKey, const uint256& tokenID, const COutPoint& outpoint);
    std::vector<COutPoint> GetTokenOutpointsForAddress(const CScript& scriptPubKey, const uint256& tokenID) const;

    // --- Undo data ('z') ---
    bool ReadTokenBlockUndo(const uint256& blockHash, CTokenBlockUndo& undo) const;
    bool WriteTokenBlockUndo(const uint256& blockHash, const CTokenBlockUndo& undo);
    bool EraseTokenBlockUndo(const uint256& blockHash);
};

#endif // CIVICNET_TOKEN_TOKENDB_H
