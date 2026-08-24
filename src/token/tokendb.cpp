// Copyright (c) 2026 The CivicNet developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <token/tokendb.h>
#include <hash.h>

namespace {

static const char DB_TOKEN_REGISTRY = 't';
static const char DB_TOKEN_COIN     = 'u';
static const char DB_TOKEN_ADDR     = 'a';
static const char DB_TOKEN_UNDO     = 'z';

struct TokenRegistryKey {
    char key;
    uint256 tokenID;
    TokenRegistryKey() : key(DB_TOKEN_REGISTRY) {}
    explicit TokenRegistryKey(const uint256& id) : key(DB_TOKEN_REGISTRY), tokenID(id) {}
    SERIALIZE_METHODS(TokenRegistryKey, obj) { READWRITE(obj.key, obj.tokenID); }
};

struct TokenCoinKey {
    char key;
    COutPoint outpoint;
    TokenCoinKey() : key(DB_TOKEN_COIN) {}
    explicit TokenCoinKey(const COutPoint& o) : key(DB_TOKEN_COIN), outpoint(o) {}
    SERIALIZE_METHODS(TokenCoinKey, obj) { READWRITE(obj.key, obj.outpoint.hash, VARINT(obj.outpoint.n)); }
};

struct TokenAddrKey {
    char key;
    uint160 scriptHash;
    uint256 tokenID;
    COutPoint outpoint;
    TokenAddrKey() : key(DB_TOKEN_ADDR) {}
    TokenAddrKey(const uint160& sh, const uint256& id, const COutPoint& o)
        : key(DB_TOKEN_ADDR), scriptHash(sh), tokenID(id), outpoint(o) {}
    SERIALIZE_METHODS(TokenAddrKey, obj)
    {
        READWRITE(obj.key, obj.scriptHash, obj.tokenID, obj.outpoint.hash, VARINT(obj.outpoint.n));
    }
};

struct TokenUndoKey {
    char key;
    uint256 blockHash;
    TokenUndoKey() : key(DB_TOKEN_UNDO) {}
    explicit TokenUndoKey(const uint256& h) : key(DB_TOKEN_UNDO), blockHash(h) {}
    SERIALIZE_METHODS(TokenUndoKey, obj) { READWRITE(obj.key, obj.blockHash); }
};

} // namespace

CTokenDB::CTokenDB(fs::path ldb_path, size_t nCacheSize, bool fMemory, bool fWipe) :
    m_db(std::make_unique<CDBWrapper>(ldb_path, nCacheSize, fMemory, fWipe, /*obfuscate=*/true))
{
}

// --- Registry ---
bool CTokenDB::ReadTokenRegistry(const uint256& tokenID, CTokenRegistryEntry& entry) const
{
    return m_db->Read(TokenRegistryKey(tokenID), entry);
}
bool CTokenDB::WriteTokenRegistry(const uint256& tokenID, const CTokenRegistryEntry& entry)
{
    return m_db->Write(TokenRegistryKey(tokenID), entry);
}
bool CTokenDB::EraseTokenRegistry(const uint256& tokenID)
{
    return m_db->Erase(TokenRegistryKey(tokenID));
}
bool CTokenDB::HaveTokenRegistry(const uint256& tokenID) const
{
    return m_db->Exists(TokenRegistryKey(tokenID));
}

// --- Token-UTXO set ---
bool CTokenDB::ReadTokenCoin(const COutPoint& outpoint, CTokenCoin& coin) const
{
    return m_db->Read(TokenCoinKey(outpoint), coin);
}
bool CTokenDB::WriteTokenCoin(const COutPoint& outpoint, const CTokenCoin& coin)
{
    return m_db->Write(TokenCoinKey(outpoint), coin);
}
bool CTokenDB::EraseTokenCoin(const COutPoint& outpoint)
{
    return m_db->Erase(TokenCoinKey(outpoint));
}
bool CTokenDB::HaveTokenCoin(const COutPoint& outpoint) const
{
    return m_db->Exists(TokenCoinKey(outpoint));
}

// --- Address index ---
bool CTokenDB::WriteTokenAddressIndex(const CScript& scriptPubKey, const uint256& tokenID, const COutPoint& outpoint)
{
    uint160 scriptHash = Hash160(scriptPubKey);
    return m_db->Write(TokenAddrKey(scriptHash, tokenID, outpoint), (unsigned char)0);
}
bool CTokenDB::EraseTokenAddressIndex(const CScript& scriptPubKey, const uint256& tokenID, const COutPoint& outpoint)
{
    uint160 scriptHash = Hash160(scriptPubKey);
    return m_db->Erase(TokenAddrKey(scriptHash, tokenID, outpoint));
}
std::vector<COutPoint> CTokenDB::GetTokenOutpointsForAddress(const CScript& scriptPubKey, const uint256& tokenID) const
{
    std::vector<COutPoint> result;
    uint160 scriptHash = Hash160(scriptPubKey);
    std::unique_ptr<CDBIterator> pcursor(m_db->NewIterator());
    pcursor->Seek(TokenAddrKey(scriptHash, tokenID, COutPoint()));
    while (pcursor->Valid()) {
        TokenAddrKey key;
        try {
            if (!pcursor->GetKey(key)) break;
        } catch (const std::exception&) {
            // Cursor ran past this prefix into a differently-shaped key -- stop, don't crash.
            break;
        }
        if (key.key != DB_TOKEN_ADDR || key.scriptHash != scriptHash || key.tokenID != tokenID) break;
        result.push_back(key.outpoint);
        pcursor->Next();
    }
    return result;
}

// --- Undo data ---
bool CTokenDB::ReadTokenBlockUndo(const uint256& blockHash, CTokenBlockUndo& undo) const
{
    return m_db->Read(TokenUndoKey(blockHash), undo);
}
bool CTokenDB::WriteTokenBlockUndo(const uint256& blockHash, const CTokenBlockUndo& undo)
{
    return m_db->Write(TokenUndoKey(blockHash), undo);
}
bool CTokenDB::EraseTokenBlockUndo(const uint256& blockHash)
{
    return m_db->Erase(TokenUndoKey(blockHash));
}
