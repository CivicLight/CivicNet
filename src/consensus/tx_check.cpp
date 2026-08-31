// Copyright (c) 2017-2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/tx_check.h>

#include <primitives/transaction.h>
#include <consensus/validation.h>
#include <hash.h>

bool CheckTransaction(const CTransaction& tx, TxValidationState& state)
{
    // Basic checks that don't depend on any context
    if (!tx.IsMWEBOnly()) {
        if (tx.vin.empty())
            return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-txns-vin-empty");
        if (tx.vout.empty())
            return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-txns-vout-empty");
    }
	
    // Size limits (this doesn't take the witness into account, as that hasn't been checked for malleability)
    if (::GetSerializeSize(tx, PROTOCOL_VERSION | SERIALIZE_TRANSACTION_NO_WITNESS | SERIALIZE_NO_MWEB) * WITNESS_SCALE_FACTOR > MAX_BLOCK_WEIGHT)
        return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-txns-oversize");

    // Check for negative or overflow output values (see CVE-2010-5139)
    CAmount nValueOut = 0;
    for (const auto& txout : tx.vout)
    {
        if (txout.nValue < 0)
            return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-txns-vout-negative");
        if (txout.nValue > MAX_MONEY)
            return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-txns-vout-toolarge");
        nValueOut += txout.nValue;
        if (!MoneyRange(nValueOut))
            return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-txns-txouttotal-toolarge");
    }

    // Check for duplicate inputs (see CVE-2018-17144)
    // While Consensus::CheckTxInputs does check if all inputs of a tx are available, and UpdateCoins marks all inputs
    // of a tx as spent, it does not check if the tx has duplicate inputs.
    // Failure to run this check will result in either a crash or an inflation bug, depending on the implementation of
    // the underlying coins database.
    std::set<COutPoint> vInOutPoints;
    for (const auto& txin : tx.vin) {
        if (!vInOutPoints.insert(txin.prevout).second)
            return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-txns-inputs-duplicate");
    }

    if (tx.IsCoinBase())
    {
        if (tx.vin[0].scriptSig.size() < 2 || tx.vin[0].scriptSig.size() > 100)
            return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-cb-length");
    }
    else
    {
        for (const auto& txin : tx.vin)
            if (txin.prevout.IsNull())
                return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-txns-prevout-null");
    }

    // --- Hybrid Value Layer: stateless token checks ---
    if (tx.nTokenTxType == TOKEN_TX_ISSUE) {
        const CTokenIssuePayload& p = tx.tokenIssuePayload;

        if (p.symbol.empty() || p.symbol.size() > MAX_TOKEN_SYMBOL_LEN)
            return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-token-symbol-len");
        for (char c : p.symbol) {
            if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')))
                return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-token-symbol-charset");
        }
        if (p.name.size() > MAX_TOKEN_NAME_LEN)
            return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-token-name-len");
        if (p.nTokenType != TOKEN_TYPE_STANDARD && p.nTokenType != TOKEN_TYPE_VESTING)
            return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-token-type");
        if (p.nDecimals > MAX_TOKEN_DECIMALS)
            return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-token-decimals");
        // Initial issuance is capped at MAX_TOKEN_SUPPLY_CAP regardless of type
        // or the TOKEN_FLAG_CAPPED flag. When TOKEN_FLAG_CAPPED is set,
        // nSupplyCap (the ceiling on later TOKEN_TX_MINT top-ups) is
        // issuer-chosen and NOT bounded by MAX_TOKEN_SUPPLY_CAP -- only the
        // initial mint is capped here.
        if (p.nInitialSupply == 0 || p.nInitialSupply > MAX_TOKEN_SUPPLY_CAP)
            return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-token-initial-supply");
        if (p.nTokenType == TOKEN_TYPE_VESTING) {
            if (p.nVestingDurationBlocks == 0)
                return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-token-vesting-duration");
            if (p.nVestingCliffBlocks > p.nVestingDurationBlocks)
                return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-token-vesting-cliff");
        } else {
            if (p.nVestingStartHeight != 0 || p.nVestingDurationBlocks != 0 || p.nVestingCliffBlocks != 0)
                return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-token-vesting-fields-nonzero");
        }
        // TOKEN_FLAG_CAPPED is orthogonal to nTokenType -- a vesting token can
        // also be mintable. This is the trust-relevant signal: absence of this
        // flag means the token is permanently fixed-supply from issuance.
        if (p.IsCapped()) {
            if (p.nSupplyCap < p.nInitialSupply)
                return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-token-supply-cap");
        } else {
            if (p.nSupplyCap != 0 || p.nMintAuthorityExpiryHeight != 0)
                return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-token-supply-cap-fields-nonzero");
        }

        // Transfer-fee mode is fixed permanently at issuance -- no update tx
        // exists for either mode (deliberately immutable, see TokenFeeMode).
        if (p.HasTransferFee()) {
            if (p.nFeeMode == TOKEN_FEE_MODE_RECIPIENT_CURVE) {
                if (p.nFeeBpsStart > 10000 || p.nFeeBpsEnd > 10000)
                    return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-token-fee-bps-range");
                if (p.feeRecipientHash160.IsNull())
                    return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-token-fee-recipient-null");
                if (p.nFeeBpsBurn != 0)
                    return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-token-fee-burn-field-nonzero");
            } else if (p.nFeeMode == TOKEN_FEE_MODE_BURN_FLAT) {
                if (p.nFeeBpsBurn > 10000)
                    return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-token-fee-bps-range");
                if (p.nFeeBpsStart != 0 || p.nFeeBpsEnd != 0 || p.nFeeDecayDurationBlocks != 0 || !p.feeRecipientHash160.IsNull())
                    return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-token-fee-curve-fields-nonzero");
            } else {
                return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-token-fee-mode");
            }
        } else {
            if (p.nFeeMode != TOKEN_FEE_MODE_NONE || p.nFeeBpsStart != 0 || p.nFeeBpsEnd != 0 ||
                p.nFeeDecayDurationBlocks != 0 || !p.feeRecipientHash160.IsNull() || p.nFeeBpsBurn != 0)
                return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-token-fee-fields-nonzero");
        }

        // tokenID = hash of the FIRST input's outpoint being spent -- NOT tx.GetHash().
        // Using this tx's own hash would be self-referential and literally uncomputable:
        // the hash depends on the very outputs that would need to embed it. Hashing the
        // committed first input instead is knowable before the tx is even fully built
        // (it only depends on which UTXO the issuer chooses to spend), and is just as
        // globally unique since a given outpoint can only ever be spent once.
        if (tx.vin.empty())
            return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-token-issue-no-inputs");
        uint256 expectedTokenID = SerializeHash(tx.vin[0].prevout);

        // Minted token outputs (tokenID == the computed token ID above) must sum exactly to nInitialSupply.
        uint64_t nMinted = 0;
        for (const CTxOut& out : tx.vout) {
            if (out.tokenID == expectedTokenID) {
                if (out.nTokenAmount == 0)
                    return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-token-issue-zero-output");
                nMinted += out.nTokenAmount;
            }
        }
        if (nMinted != p.nInitialSupply)
            return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-token-issue-mint-mismatch");
        // Vesting tokens: the initial supply must be minted into exactly ONE
        // vesting-lock output (not a plain address output), with its packed
        // schedule fields matching this payload's vesting fields exactly --
        // this is what actually binds/enforces the schedule, the payload
        // fields alone (checked earlier) are just format validation.
        if (p.IsVesting()) {
            int nVestingLockOutputs = 0;
            for (const CTxOut& out : tx.vout) {
                if (out.tokenID != expectedTokenID) continue;
                CTokenVestingLockData vd;
                if (!IsTokenVestingLockScript(out.scriptPubKey, &vd))
                    return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-token-issue-vesting-not-locked");
                if (vd.tokenID != expectedTokenID)
                    return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-token-issue-vesting-tokenid-mismatch");
                if (vd.nOriginalTotalAmount != out.nTokenAmount)
                    return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-token-issue-vesting-amount-mismatch");
                if (vd.nStartHeight != p.nVestingStartHeight || vd.nCliffBlocks != p.nVestingCliffBlocks || vd.nDurationBlocks != p.nVestingDurationBlocks)
                    return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-token-issue-vesting-schedule-mismatch");
                nVestingLockOutputs++;
            }
            if (nVestingLockOutputs != 1)
                return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-token-issue-vesting-output-count");
        }
    } else if (tx.nTokenTxType == TOKEN_TX_CONVERT_OUT) {
        const CTokenConvertOutPayload& p = tx.tokenConvertOutPayload;
        if (p.tokenID.IsNull() || p.nTokenAmountBurned == 0)
            return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-token-convert-out-payload");
    } else if (tx.nTokenTxType == TOKEN_TX_VESTING_RELEASE) {
        const CTokenVestingReleasePayload& p = tx.tokenVestingReleasePayload;
        if (p.tokenID.IsNull() || p.nAmountToRelease == 0)
            return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-token-vesting-release-payload");
    } else if (tx.nTokenTxType == TOKEN_TX_BURN) {
        const CTokenBurnPayload& p = tx.tokenBurnPayload;
        if (p.tokenID.IsNull() || p.nAmountToBurn == 0)
            return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-token-burn-payload");
    } else if (tx.nTokenTxType == TOKEN_TX_MINT) {
        const CTokenMintPayload& p = tx.tokenMintPayload;
        if (p.tokenID.IsNull() || p.nAmountToMint == 0)
            return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-token-mint-payload");
        // Minted token outputs (tokenID == p.tokenID) must sum exactly to nAmountToMint.
        uint64_t nMinted = 0;
        for (const CTxOut& out : tx.vout) {
            if (out.tokenID == p.tokenID) {
                if (out.nTokenAmount == 0)
                    return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-token-mint-zero-output");
                nMinted += out.nTokenAmount;
            }
        }
        if (nMinted != p.nAmountToMint)
            return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-token-mint-mismatch");
    }

    // Every CTxOut's token field pair must be consistent: both null/zero, or both set.
    for (const CTxOut& out : tx.vout) {
        if (out.tokenID.IsNull() != (out.nTokenAmount == 0))
            return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-token-output-malformed");
    }

    return true;
}
