// Copyright (c) 2026 The CivicNet developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CIVICNET_TOKEN_TOKENTX_H
#define CIVICNET_TOKEN_TOKENTX_H

#include <serialize.h>
#include <uint256.h>
#include <amount.h>
#include <script/script.h>
#include <string>
#include <vector>

static const unsigned int MAX_TOKEN_SYMBOL_LEN = 12;
static const unsigned int MAX_TOKEN_NAME_LEN   = 32;
static const uint8_t      MAX_TOKEN_DECIMALS   = 8;
static const uint64_t     MAX_TOKEN_SUPPLY_CAP = 1000000000ULL; // 1 miliar (Tier 1 cap)

// TODO: set real deploy time before mainnet activation (0 = active immediately, dev/test only)
static const int64_t HYBRID_VALUE_LAYER_ACTIVATION_TIME = 0;

/** Builds the consensus-recognized reserve-custody script for a token:
 *  PUSH(32-byte tokenID) OP_TOKEN_RESERVE. Funds sent here are NOT spendable by
 *  any private key -- only a valid TX_CONVERT_OUT redemption (validated in
 *  ConnectBlock, outside normal script evaluation) can release them. */
inline CScript BuildTokenReserveScript(const uint256& tokenID)
{
    CScript script;
    script << std::vector<unsigned char>(tokenID.begin(), tokenID.end()) << OP_TOKEN_RESERVE;
    return script;
}

/** Recognizes the reserve-custody script template. If tokenIDOut is non-null
 *  and the script matches, the embedded tokenID is written to it. */
inline bool IsTokenReserveScript(const CScript& script, uint256* tokenIDOut = nullptr)
{
    if (script.size() != 34) return false;
    if (script[0] != 0x20) return false; // push-32-bytes opcode
    if (script[33] != OP_TOKEN_RESERVE) return false;
    if (tokenIDOut) {
        *tokenIDOut = uint256(std::vector<unsigned char>(script.begin() + 1, script.begin() + 33));
    }
    return true;
}

// TODO: promote to the versioned-constant activation-gate pattern used
// elsewhere (MIN_TOKEN_LOCK_AMOUNT -> _V2 -> _V3) before mainnet deploy --
// hardcoded starting value for now (1000 CIVIC), per earlier design.
inline CAmount GetMinTokenLockAmount(int nHeight)
{
    (void)nHeight;
    return 1000 * COIN;
}

/** Nilai byte `nTokenTxType` di dalam payload -- BUKAN CTransaction::nVersion. */
enum TokenTxType : uint8_t {
    TOKEN_TX_NONE         = 0, // transfer biasa, cuma CTxOut token field yg dipakai
    TOKEN_TX_ISSUE        = 1,
    TOKEN_TX_CONVERT_OUT  = 2,
};

enum TokenType : uint8_t {
    TOKEN_TYPE_STANDARD = 1,
    TOKEN_TYPE_VESTING  = 2,
    // Type 3 (capped extra-supply/re-mintable) & Type 4 (transfer-fee) -- deferred
};

enum TokenFlags : uint32_t {
    TOKEN_FLAG_HAS_POE_ANCHOR = (1 << 0),
    // bit1..31 reserved buat Tier 2/3
};

/** Payload TOKEN_TX_ISSUE. Diserialize di dalam blok `flags & 2` extended-tx,
 *  setelah byte nTokenTxType (lihat pola witness/MWEB di transaction.h). */
class CTokenIssuePayload
{
public:
    std::string symbol;              // max MAX_TOKEN_SYMBOL_LEN, charset A-Z0-9
    std::string name;                // max MAX_TOKEN_NAME_LEN
    uint8_t     nTokenType;
    uint8_t     nDecimals;
    uint64_t    nInitialSupply;
    uint32_t    nFlags;
    uint256     poeAnchorHash;       // all-zero kalau TOKEN_FLAG_HAS_POE_ANCHOR gak diset

    // hanya diserialize kalau nTokenType == TOKEN_TYPE_VESTING (lihat SerializationOps)
    uint32_t    nVestingStartHeight;
    uint32_t    nVestingDurationBlocks;
    uint32_t    nVestingCliffBlocks;

    CTokenIssuePayload() { SetNull(); }

    void SetNull()
    {
        symbol.clear();
        name.clear();
        nTokenType = 0;
        nDecimals = 0;
        nInitialSupply = 0;
        nFlags = 0;
        poeAnchorHash.SetNull();
        nVestingStartHeight = 0;
        nVestingDurationBlocks = 0;
        nVestingCliffBlocks = 0;
    }

    bool IsVesting() const { return nTokenType == TOKEN_TYPE_VESTING; }
    bool HasPoEAnchor() const { return (nFlags & TOKEN_FLAG_HAS_POE_ANCHOR) != 0; }

    SERIALIZE_METHODS(CTokenIssuePayload, obj)
    {
        READWRITE(obj.symbol);
        READWRITE(obj.name);
        READWRITE(obj.nTokenType);
        READWRITE(obj.nDecimals);
        READWRITE(obj.nInitialSupply);
        READWRITE(obj.nFlags);
        READWRITE(obj.poeAnchorHash);
        // CONDITIONAL: aman karena obj.nTokenType sudah ke-assign di baris di atas
        // SEBELUM titik ini, baik pas serialize (obj sudah lengkap dari awal) maupun
        // pas deserialize (nTokenType sudah dibaca duluan) -- simetris, gak butuh
        // cek arah baca/tulis. Pola sama seperti witness/MWEB flag-based conditional
        // serialize yang sudah dipakai di primitives/transaction.h.
        if (obj.nTokenType == TOKEN_TYPE_VESTING) {
            READWRITE(obj.nVestingStartHeight);
            READWRITE(obj.nVestingDurationBlocks);
            READWRITE(obj.nVestingCliffBlocks);
        }
    }
};

/** Payload TOKEN_TX_CONVERT_OUT. */
class CTokenConvertOutPayload
{
public:
    uint256  tokenID;
    uint64_t nTokenAmountBurned;

    CTokenConvertOutPayload() { SetNull(); }

    void SetNull()
    {
        tokenID.SetNull();
        nTokenAmountBurned = 0;
    }

    SERIALIZE_METHODS(CTokenConvertOutPayload, obj)
    {
        READWRITE(obj.tokenID);
        READWRITE(obj.nTokenAmountBurned);
    }
};

#endif // CIVICNET_TOKEN_TOKENTX_H

// DIAGNOSTIC_MARKER_TEST_12345
