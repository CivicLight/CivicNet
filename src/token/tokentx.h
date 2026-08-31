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
static const uint64_t     MAX_TOKEN_SUPPLY_CAP = 1000000000ULL; // 1 billion (Tier 1 cap)
static const unsigned int MAX_METADATA_URI_LEN = 256; // metadataUri max length, off-chain content (IPFS/HTTP) pointer

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

/** Metadata embedded in a vesting-lock script's push data. */
struct CTokenVestingLockData
{
    uint256  tokenID;
    uint160  beneficiaryHash160;
    uint32_t nStartHeight;
    uint32_t nCliffBlocks;
    uint32_t nDurationBlocks;
    uint64_t nOriginalTotalAmount;
};

/** Builds the consensus-recognized vesting-custody script for a token:
 *  PUSH(72-byte packed vesting data) OP_TOKEN_VESTING_LOCK. Funds sent here
 *  are NOT spendable by any private key -- only a valid TX_VESTING_RELEASE,
 *  authorized by an input matching beneficiaryHash160, can release the
 *  vested portion (validated in ConnectBlock, outside normal script
 *  evaluation). Mirrors BuildTokenReserveScript's pattern exactly. */
inline CScript BuildTokenVestingLockScript(const CTokenVestingLockData& d)
{
    std::vector<unsigned char> data;
    data.insert(data.end(), d.tokenID.begin(), d.tokenID.end());
    data.insert(data.end(), d.beneficiaryHash160.begin(), d.beneficiaryHash160.end());
    for (int i = 0; i < 4; i++) data.push_back((unsigned char)((d.nStartHeight >> (8 * i)) & 0xff));
    for (int i = 0; i < 4; i++) data.push_back((unsigned char)((d.nCliffBlocks >> (8 * i)) & 0xff));
    for (int i = 0; i < 4; i++) data.push_back((unsigned char)((d.nDurationBlocks >> (8 * i)) & 0xff));
    for (int i = 0; i < 8; i++) data.push_back((unsigned char)((d.nOriginalTotalAmount >> (8 * i)) & 0xff));
    CScript script;
    script << data << OP_TOKEN_VESTING_LOCK;
    return script;
}

/** Recognizes the vesting-lock script template and unpacks its embedded
 *  data. Push data is 72 bytes: 32 (tokenID) + 20 (beneficiaryHash160) +
 *  4+4+4 (heights/blocks) + 8 (original amount) = 72. Total script size is
 *  1 (push-72 opcode) + 72 (data) + 1 (OP_TOKEN_VESTING_LOCK) = 74 bytes. */
inline bool IsTokenVestingLockScript(const CScript& script, CTokenVestingLockData* dataOut = nullptr)
{
    if (script.size() != 74) return false;
    if (script[0] != 0x48) return false; // push-72-bytes opcode (0x48 = 72)
    if (script[73] != OP_TOKEN_VESTING_LOCK) return false;
    if (dataOut) {
        auto it = script.begin() + 1;
        dataOut->tokenID = uint256(std::vector<unsigned char>(it, it + 32)); it += 32;
        dataOut->beneficiaryHash160 = uint160(std::vector<unsigned char>(it, it + 20)); it += 20;
        uint32_t start = 0, cliff = 0, duration = 0;
        for (int i = 0; i < 4; i++) start |= ((uint32_t)(*it++)) << (8 * i);
        for (int i = 0; i < 4; i++) cliff |= ((uint32_t)(*it++)) << (8 * i);
        for (int i = 0; i < 4; i++) duration |= ((uint32_t)(*it++)) << (8 * i);
        uint64_t total = 0;
        for (int i = 0; i < 8; i++) total |= ((uint64_t)(*it++)) << (8 * i);
        dataOut->nStartHeight = start;
        dataOut->nCliffBlocks = cliff;
        dataOut->nDurationBlocks = duration;
        dataOut->nOriginalTotalAmount = total;
    }
    return true;
}

/** Computes the cumulative amount that should be unlocked by nHeight, given
 *  the vesting schedule. Cliff-gated, then linear to nOriginalTotalAmount by
 *  nStartHeight + nDurationBlocks. Never exceeds nOriginalTotalAmount. */
inline uint64_t GetVestedCumulativeAmount(const CTokenVestingLockData& d, int nHeight)
{
    uint64_t cliffHeight = (uint64_t)d.nStartHeight + d.nCliffBlocks;
    if ((uint64_t)nHeight < cliffHeight) return 0;
    uint64_t endHeight = (uint64_t)d.nStartHeight + d.nDurationBlocks;
    if ((uint64_t)nHeight >= endHeight) return d.nOriginalTotalAmount;
    if (d.nDurationBlocks == 0) return d.nOriginalTotalAmount; // guarded upstream, defense in depth
    uint64_t elapsed = (uint64_t)nHeight - d.nStartHeight;
    // 128-bit intermediate via unsigned __int128 avoids overflow for
    // (amount * elapsed) before dividing -- amount up to ~1e9*1e8 and
    // elapsed up to ~4 billion could otherwise overflow a 64-bit product.
    unsigned __int128 num = (unsigned __int128)d.nOriginalTotalAmount * (unsigned __int128)elapsed;
    unsigned __int128 result = num / (unsigned __int128)d.nDurationBlocks;
    return (uint64_t)result;
}

// TODO: promote to the versioned-constant activation-gate pattern used
// elsewhere (MIN_TOKEN_LOCK_AMOUNT -> _V2 -> _V3) before mainnet deploy --
// hardcoded starting value for now (1000 CIVIC), per earlier design.
inline CAmount GetMinTokenLockAmount(int nHeight)
{
    (void)nHeight;
    return 1000 * COIN;
}

// --- TOKEN_TX_ISSUE anti-spam fee: volume-adaptive, split burn/miner. ---
// Hard ceiling on the base issuance fee regardless of block-reward-based
// calculation (see GetFeeBurnMultiplierPct() in tokenvalidation.cpp, which
// multiplies the current block subsidy) -- caps the fee at a predictable
// maximum even if CIVIC's block reward or market price moves a lot.
static const CAmount MAX_ISSUANCE_FEE = 50 * COIN;

// Rolling window (in blocks, ~24h at 60s block time) used to measure recent
// TX_ISSUE volume network-wide -- NOT per-token, a single global counter.
// Same window length as the (now-superseded) rate-vote design discussion,
// reused here since it's still a reasonable "one day" cadence.
static const int VOLUME_WINDOW_BLOCKS = 1440;

// Volume tiers: fewer than VOLUME_TIER1_MAX issuances in the trailing
// window pays the full fee; each higher tier discounts it, so heavy organic
// adoption isn't taxed at the same flat rate as a quiet network. Volume
// count used against these thresholds is measured BEFORE the current
// issuance (i.e. how many happened in the window prior to this one).
static const size_t VOLUME_TIER1_MAX = 50;   // < 50: 100% fee
static const size_t VOLUME_TIER2_MAX = 200;  // 50-200: 50% fee
                                              // > 200: 20% fee

/** Returns the fee percentage (0-100) that applies for a given trailing
 *  issuance-volume count. Pure function, no chain-state access needed. */
inline int GetVolumeFeeTierPct(size_t volumeCount)
{
    if (volumeCount < VOLUME_TIER1_MAX) return 100;
    if (volumeCount < VOLUME_TIER2_MAX) return 50;
    return 20;
}

/** The `nTokenTxType` byte value inside the payload -- NOT CTransaction::nVersion. */
enum TokenTxType : uint8_t {
    TOKEN_TX_NONE         = 0, // plain transfer, only CTxOut token fields used
    TOKEN_TX_ISSUE        = 1,
    TOKEN_TX_CONVERT_OUT  = 2,
    TOKEN_TX_MINT         = 3, // supply top-up, TOKEN_TYPE_CAPPED only, issuer-authorized
    TOKEN_TX_VESTING_RELEASE = 4, // claim vested portion, beneficiary-authorized
    TOKEN_TX_BURN         = 5, // permanently destroy owned tokens, self-authorized (holder's own signature only, no issuer/authority involvement)
    TOKEN_TX_METADATA_UPDATE = 6, // update metadataUri/metadataHash, issuer-authorized (mirrors TOKEN_TX_MINT's auth pattern); rejected if fMetadataImmutable is set
};

enum TokenType : uint8_t {
    TOKEN_TYPE_STANDARD = 1,
    TOKEN_TYPE_VESTING  = 2,
    // Type 4 (transfer-fee) -- deferred, needs plain-transfer (TOKEN_TX_NONE) support first
};

// TOKEN_FLAG_CAPPED is orthogonal to TokenType (distribution mechanism) --
// e.g. a token can be TOKEN_TYPE_VESTING *and* TOKEN_FLAG_CAPPED at once.
// This flag is the trust-relevant signal: a token WITHOUT it is permanently
// fixed-supply from issuance (equivalent to an SPL token with no mint
// authority, or a revoked one), and this can never change after the fact.
// A token WITH it has an issuer-controlled mint authority, capped at
// nSupplyCap and optionally expiring at nMintAuthorityExpiryHeight -- surface
// this plainly in gettokeninfo so holders can judge inflation risk themselves.
enum TokenFlags : uint32_t {
    TOKEN_FLAG_HAS_POE_ANCHOR = (1 << 0),
    TOKEN_FLAG_CAPPED         = (1 << 1), // mintable up to nSupplyCap, issuer-authorized
    TOKEN_FLAG_TRANSFER_FEE   = (1 << 2), // fee-on-transfer, mode/params fixed at issuance -- see TokenFeeMode
    TOKEN_FLAG_HAS_METADATA   = (1 << 3), // metadataUri/metadataHash present, updatable via TOKEN_TX_METADATA_UPDATE unless fMetadataImmutable
    // bit4..31 reserved for Tier 2/3
};

/** Transfer-fee mode, fixed permanently at issuance -- there is no update
 *  transaction for this in either mode; both are deliberately immutable
 *  (no smart contracts here, so no per-token custom fee logic is possible --
 *  these two native modes cover the two dominant real-world motivations for
 *  fee-on-transfer: bootstrap-then-settle deterministic fee curves, and flat
 *  deflationary burn). See GetCurrentFeeBps()/ApplyTokenTx's TOKEN_TX_NONE
 *  branch for how each mode is applied. */
enum TokenFeeMode : uint8_t {
    TOKEN_FEE_MODE_NONE            = 0,
    TOKEN_FEE_MODE_RECIPIENT_CURVE = 1, // linear decay feeBpsStart -> feeBpsEnd over feeDecayDurationBlocks, then flat at feeBpsEnd; paid to feeRecipientHash160
    TOKEN_FEE_MODE_BURN_FLAT       = 2, // flat feeBpsBurn, burned (reduces nCurrentSupply), no recipient
};

/** TOKEN_TX_ISSUE payload. Serialized inside the `flags & 2` extended-tx
 *  block, after the nTokenTxType byte (same pattern as witness/MWEB in transaction.h). */
class CTokenIssuePayload
{
public:
    std::string symbol;              // max MAX_TOKEN_SYMBOL_LEN, charset A-Z0-9
    std::string name;                // max MAX_TOKEN_NAME_LEN
    uint8_t     nTokenType;
    uint8_t     nDecimals;
    uint64_t    nInitialSupply;
    uint32_t    nFlags;
    uint256     poeAnchorHash;       // all-zero unless TOKEN_FLAG_HAS_POE_ANCHOR is set

    // only serialized when nFlags & TOKEN_FLAG_HAS_METADATA
    std::string metadataUri;         // off-chain pointer (IPFS/HTTP), max MAX_METADATA_URI_LEN
    uint256     metadataHash;        // SHA256 of the off-chain metadata JSON content, for tamper detection
    bool        fMetadataImmutable;  // if true, TOKEN_TX_METADATA_UPDATE is permanently rejected for this token

    // only serialized when nTokenType == TOKEN_TYPE_VESTING (see SERIALIZE_METHODS)
    uint32_t    nVestingStartHeight;
    uint32_t    nVestingDurationBlocks;
    uint32_t    nVestingCliffBlocks;

    // only serialized when nFlags & TOKEN_FLAG_CAPPED. Hard ceiling on
    // nCurrentSupply across all TOKEN_TX_ISSUE + TOKEN_TX_MINT for this token.
    uint64_t    nSupplyCap;
    // only serialized when nFlags & TOKEN_FLAG_CAPPED. Block height after which
    // no further TOKEN_TX_MINT is valid for this token, even if nCurrentSupply
    // has not reached nSupplyCap. 0 = no expiry (mint authority lasts until
    // the cap is reached). Lets an issuer commit up front, immutably, to a
    // hard deadline on new supply -- stronger than a revocable mint authority.
    uint32_t    nMintAuthorityExpiryHeight;

    // only serialized when nFlags & TOKEN_FLAG_TRANSFER_FEE. nFeeMode selects
    // which sub-fields below are meaningful/serialized further.
    uint8_t     nFeeMode;
    // TOKEN_FEE_MODE_RECIPIENT_CURVE fields:
    uint16_t    nFeeBpsStart;
    uint16_t    nFeeBpsEnd;
    uint32_t    nFeeDecayDurationBlocks;
    uint160     feeRecipientHash160;
    // TOKEN_FEE_MODE_BURN_FLAT field:
    uint16_t    nFeeBpsBurn;

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
        metadataUri.clear();
        metadataHash.SetNull();
        fMetadataImmutable = false;
        nVestingStartHeight = 0;
        nVestingDurationBlocks = 0;
        nVestingCliffBlocks = 0;
        nSupplyCap = 0;
        nMintAuthorityExpiryHeight = 0;
        nFeeMode = TOKEN_FEE_MODE_NONE;
        nFeeBpsStart = 0;
        nFeeBpsEnd = 0;
        nFeeDecayDurationBlocks = 0;
        feeRecipientHash160.SetNull();
        nFeeBpsBurn = 0;
    }

    bool IsVesting() const { return nTokenType == TOKEN_TYPE_VESTING; }
    bool IsCapped() const { return (nFlags & TOKEN_FLAG_CAPPED) != 0; }
    bool HasPoEAnchor() const { return (nFlags & TOKEN_FLAG_HAS_POE_ANCHOR) != 0; }
    bool HasTransferFee() const { return (nFlags & TOKEN_FLAG_TRANSFER_FEE) != 0; }
    bool HasMetadata() const { return (nFlags & TOKEN_FLAG_HAS_METADATA) != 0; }

    SERIALIZE_METHODS(CTokenIssuePayload, obj)
    {
        READWRITE(obj.symbol);
        READWRITE(obj.name);
        READWRITE(obj.nTokenType);
        READWRITE(obj.nDecimals);
        READWRITE(obj.nInitialSupply);
        READWRITE(obj.nFlags);
        READWRITE(obj.poeAnchorHash);
        if (obj.nFlags & TOKEN_FLAG_HAS_METADATA) {
            READWRITE(obj.metadataUri);
            READWRITE(obj.metadataHash);
            READWRITE(obj.fMetadataImmutable);
        }
        // CONDITIONAL: safe because obj.nTokenType/nFlags are already assigned
        // above this point, both when serializing (obj is fully populated from
        // the start) and when deserializing (both fields were just read) --
        // symmetric, no read/write direction check needed. Same pattern as the
        // witness/MWEB flag-based conditional serialization already used in
        // primitives/transaction.h.
        if (obj.nTokenType == TOKEN_TYPE_VESTING) {
            READWRITE(obj.nVestingStartHeight);
            READWRITE(obj.nVestingDurationBlocks);
            READWRITE(obj.nVestingCliffBlocks);
        }
        if (obj.nFlags & TOKEN_FLAG_CAPPED) {
            READWRITE(obj.nSupplyCap);
            READWRITE(obj.nMintAuthorityExpiryHeight);
        }
        if (obj.nFlags & TOKEN_FLAG_TRANSFER_FEE) {
            READWRITE(obj.nFeeMode);
            if (obj.nFeeMode == TOKEN_FEE_MODE_RECIPIENT_CURVE) {
                READWRITE(obj.nFeeBpsStart);
                READWRITE(obj.nFeeBpsEnd);
                READWRITE(obj.nFeeDecayDurationBlocks);
                READWRITE(obj.feeRecipientHash160);
            } else if (obj.nFeeMode == TOKEN_FEE_MODE_BURN_FLAT) {
                READWRITE(obj.nFeeBpsBurn);
            }
        }
    }
};

/** TOKEN_TX_MINT payload -- a supply top-up for a TOKEN_TYPE_CAPPED token.
 *  Authorized by an input spending from the token's issuerScriptPubKey
 *  (verified via normal script/signature evaluation -- no consensus bypass
 *  needed here since this input IS spendable, unlike the reserve-lock input). */
class CTokenMintPayload
{
public:
    uint256  tokenID;
    uint64_t nAmountToMint;

    CTokenMintPayload() { SetNull(); }

    void SetNull()
    {
        tokenID.SetNull();
        nAmountToMint = 0;
    }

    SERIALIZE_METHODS(CTokenMintPayload, obj)
    {
        READWRITE(obj.tokenID);
        READWRITE(obj.nAmountToMint);
    }
};

/** TOKEN_TX_METADATA_UPDATE payload -- updates metadataUri/metadataHash for an
 *  existing token. Authorized by an input spending from the token's
 *  issuerScriptPubKey (same pattern as TOKEN_TX_MINT). Rejected at consensus
 *  level if the token's fMetadataImmutable flag is already set. */
class CTokenMetadataUpdatePayload
{
public:
    uint256     tokenID;
    std::string metadataUri;
    uint256     metadataHash;
    bool        fSetImmutable; // if true, sets fMetadataImmutable=true on this same update (one-way lock)

    CTokenMetadataUpdatePayload() { SetNull(); }

    void SetNull()
    {
        tokenID.SetNull();
        metadataUri.clear();
        metadataHash.SetNull();
        fSetImmutable = false;
    }

    SERIALIZE_METHODS(CTokenMetadataUpdatePayload, obj)
    {
        READWRITE(obj.tokenID);
        READWRITE(obj.metadataUri);
        READWRITE(obj.metadataHash);
        READWRITE(obj.fSetImmutable);
    }
};

/** TOKEN_TX_CONVERT_OUT payload. */
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

/** TOKEN_TX_VESTING_RELEASE payload -- claims the portion of a vesting-lock
 *  UTXO that has vested by the connecting block's height. Authorized by an
 *  input in the same tx whose scriptPubKey hashes (Hash160) to the
 *  vesting-lock's beneficiaryHash160 -- verified via normal signature
 *  checking on that input, same authorization pattern as TOKEN_TX_MINT's
 *  issuerScriptPubKey check. The vesting-lock input itself is unspendable
 *  via script (like the reserve-lock input) and is skipped from signature
 *  verification via skipInputs. */
class CTokenVestingReleasePayload
{
public:
    uint256  tokenID;
    uint64_t nAmountToRelease;
    CTokenVestingReleasePayload() { SetNull(); }
    void SetNull()
    {
        tokenID.SetNull();
        nAmountToRelease = 0;
    }
    SERIALIZE_METHODS(CTokenVestingReleasePayload, obj)
    {
        READWRITE(obj.tokenID);
        READWRITE(obj.nAmountToRelease);
    }
};

/** Computes the fee-bps currently in effect for TOKEN_FEE_MODE_RECIPIENT_CURVE,
 *  given the connecting block's height. Linear decay from feeBpsStart (at
 *  issueHeight) down to feeBpsEnd (at issueHeight + decayDurationBlocks),
 *  then flat at feeBpsEnd forever after. decayDurationBlocks == 0 means the
 *  curve is already fully decayed (flat feeBpsEnd from issuance). Note
 *  feeBpsEnd may be >= feeBpsStart (ramping up is a valid curve shape too --
 *  the math is symmetric, nothing here assumes decay is downward). */
inline uint16_t GetCurrentFeeBpsForCurve(uint16_t feeBpsStart, uint16_t feeBpsEnd,
                                          uint32_t decayDurationBlocks,
                                          uint32_t issueHeight, int nHeight)
{
    if (decayDurationBlocks == 0) return feeBpsEnd;
    uint64_t endHeight = (uint64_t)issueHeight + decayDurationBlocks;
    if ((uint64_t)nHeight <= (uint64_t)issueHeight) return feeBpsStart;
    if ((uint64_t)nHeight >= endHeight) return feeBpsEnd;
    uint64_t elapsed = (uint64_t)nHeight - issueHeight;
    // Signed intermediate since feeBpsEnd - feeBpsStart may be negative
    // (ramping up); widen to int64_t before the multiply to avoid overflow
    // (max bps 10000-ish times max block-count range is still small, but
    // widen anyway for defense in depth, matching the vesting helper's style).
    int64_t startI = (int64_t)feeBpsStart;
    int64_t endI = (int64_t)feeBpsEnd;
    int64_t delta = endI - startI;
    int64_t adjustment = (delta * (int64_t)elapsed) / (int64_t)decayDurationBlocks;
    int64_t result = startI + adjustment;
    if (result < 0) result = 0;
    if (result > 10000) result = 10000;
    return (uint16_t)result;
}

/** TOKEN_TX_BURN payload -- permanently destroys owned tokens. Mirrors
 *  TOKEN_TX_MINT in reverse: authorized purely by the burner's own signature
 *  on the token-colored input(s) being spent (normal script/signature
 *  evaluation, same as any plain transfer) -- no issuer/authority check of
 *  any kind, since destroying your own tokens needs no one else's
 *  permission. Consensus requires sum(token inputs) - sum(token outputs)
 *  == nAmountToBurn exactly; any leftover must appear as a normal
 *  colored change output back to the burner (or to any address they
 *  choose), same as TOKEN_TX_NONE. nCurrentSupply is reduced permanently. */
class CTokenBurnPayload
{
public:
    uint256  tokenID;
    uint64_t nAmountToBurn;
    CTokenBurnPayload() { SetNull(); }
    void SetNull()
    {
        tokenID.SetNull();
        nAmountToBurn = 0;
    }
    SERIALIZE_METHODS(CTokenBurnPayload, obj)
    {
        READWRITE(obj.tokenID);
        READWRITE(obj.nAmountToBurn);
    }
};

#endif // CIVICNET_TOKEN_TOKENTX_H

// DIAGNOSTIC_MARKER_TEST_12345
