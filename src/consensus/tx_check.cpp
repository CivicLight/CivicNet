// Copyright (c) 2017-2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/tx_check.h>

#include <primitives/transaction.h>
#include <consensus/validation.h>

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

        // Minted token outputs (tokenID == this tx's own hash) must sum exactly to nInitialSupply.
        uint256 expectedTokenID = tx.GetHash();
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
    } else if (tx.nTokenTxType == TOKEN_TX_CONVERT_OUT) {
        const CTokenConvertOutPayload& p = tx.tokenConvertOutPayload;
        if (p.tokenID.IsNull() || p.nTokenAmountBurned == 0)
            return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-token-convert-out-payload");
    }

    // Every CTxOut's token field pair must be consistent: both null/zero, or both set.
    for (const CTxOut& out : tx.vout) {
        if (out.tokenID.IsNull() != (out.nTokenAmount == 0))
            return state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-token-output-malformed");
    }

    return true;
}
