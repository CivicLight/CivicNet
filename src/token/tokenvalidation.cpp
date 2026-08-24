// Copyright (c) 2026 The CivicNet developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <token/tokenvalidation.h>
#include <arith_uint256.h>

bool ApplyTokenTx(const CTransaction& tx, CTokenViewCache& tokenView, const CCoinsViewCache& view,
                   int nHeight, std::set<unsigned int>& skipInputs,
                   CTokenBlockUndo& txUndo, TxValidationState& tx_state)
{
    if (tx.nTokenTxType == TOKEN_TX_NONE) {
        // TODO: plain token transfers -- token input amount must equal token
        // output amount per tokenID. Not yet implemented.
        return true;
    }

    if (tx.nTokenTxType == TOKEN_TX_ISSUE) {
        const CTokenIssuePayload& p = tx.tokenIssuePayload;
        uint256 tokenID = tx.GetHash();

        // CheckTransaction() already guarantees exactly-correct mint output sum
        // and payload field validity. Here we only need the parts that require
        // chain state: the tokenID must not already exist (collision is
        // impossible in practice since tokenID == this tx's own txid, checked
        // anyway as defense in depth), and the reserve-lock output amount +
        // issuer scriptPubKey are read off the transaction to build the entry.
        if (tokenView.HaveTokenRegistry(tokenID)) {
            return tx_state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-token-issue-duplicate-id");
        }

        CAmount nReserveLocked = 0;
        int nReserveOutputs = 0;
        for (size_t o = 0; o < tx.vout.size(); o++) {
            const CTxOut& out = tx.vout[o];
            if (IsTokenReserveScript(out.scriptPubKey)) {
                nReserveOutputs++;
                nReserveLocked = out.nValue;
            }
            if (out.tokenID == tokenID && out.nTokenAmount > 0) {
                CTokenCoin newCoin;
                newCoin.tokenID = tokenID;
                newCoin.nTokenAmount = out.nTokenAmount;
                newCoin.scriptPubKey = out.scriptPubKey;
                newCoin.nHeight = (uint32_t)nHeight;
                tokenView.AddTokenCoin(COutPoint(tx.GetHash(), (uint32_t)o), newCoin);
            }
        }
        if (nReserveOutputs != 1) {
            return tx_state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-token-issue-reserve-output-count");
        }
        if (nReserveLocked < GetMinTokenLockAmount(nHeight)) {
            return tx_state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-token-issue-below-min-lock");
        }

        // Issuer identity = scriptPubKey of the tx's first input's prevout
        // (the address that funded the issuance). Used later to authorize
        // supply top-ups.
        CScript issuerScriptPubKey;
        if (!tx.vin.empty()) {
            const Coin& firstInputCoin = view.AccessCoin(tx.vin[0].prevout);
            if (!firstInputCoin.IsSpent()) {
                issuerScriptPubKey = firstInputCoin.out.scriptPubKey;
            }
        }

        CTokenRegistryEntry entry;
        entry.symbol = p.symbol;
        entry.name = p.name;
        entry.nTokenType = p.nTokenType;
        entry.nDecimals = p.nDecimals;
        entry.nInitialSupply = p.nInitialSupply;
        entry.nCurrentSupply = p.nInitialSupply;
        entry.nInitialReserveLocked = nReserveLocked;
        entry.nCurrentReserveLocked = nReserveLocked;
        entry.issuerScriptPubKey = issuerScriptPubKey;
        entry.nFlags = p.nFlags;
        entry.poeAnchorHash = p.poeAnchorHash;
        entry.issueTxid = tokenID;
        entry.nIssueHeight = (uint32_t)nHeight;
        entry.nVestingStartHeight = p.nVestingStartHeight;
        entry.nVestingDurationBlocks = p.nVestingDurationBlocks;
        entry.nVestingCliffBlocks = p.nVestingCliffBlocks;

        tokenView.SetTokenRegistry(tokenID, entry);
        txUndo.newTokenIDs.push_back(tokenID);
        return true;
    }

    if (tx.nTokenTxType == TOKEN_TX_CONVERT_OUT) {
        const CTokenConvertOutPayload& p = tx.tokenConvertOutPayload;

        CTokenRegistryEntry entry;
        if (!tokenView.GetTokenRegistry(p.tokenID, entry)) {
            return tx_state.Invalid(TxValidationResult::TX_CONSENSUS, "token-convert-out-unknown-token");
        }

        // --- Locate the reserve-redemption input (exactly one) ---
        int nReserveInputIdx = -1;
        for (size_t j = 0; j < tx.vin.size(); j++) {
            const Coin& coin = view.AccessCoin(tx.vin[j].prevout);
            if (coin.IsSpent()) continue;
            uint256 scriptTokenID;
            if (IsTokenReserveScript(coin.out.scriptPubKey, &scriptTokenID) && scriptTokenID == p.tokenID) {
                if (nReserveInputIdx != -1) {
                    return tx_state.Invalid(TxValidationResult::TX_CONSENSUS, "token-convert-out-multiple-reserve-inputs");
                }
                nReserveInputIdx = (int)j;
            }
        }
        if (nReserveInputIdx == -1) {
            return tx_state.Invalid(TxValidationResult::TX_CONSENSUS, "token-convert-out-missing-reserve-input");
        }
        const Coin& reserveCoin = view.AccessCoin(tx.vin[nReserveInputIdx].prevout);
        if (reserveCoin.out.nValue != entry.nCurrentReserveLocked) {
            return tx_state.Invalid(TxValidationResult::TX_CONSENSUS, "token-convert-out-reserve-value-mismatch");
        }
        skipInputs.insert((unsigned int)nReserveInputIdx);

        // --- Compute exact redemption amount using the token's fixed rate,
        //     via 256-bit intermediate math so burned*reserveLocked can't overflow. ---
        if (entry.nInitialSupply == 0) {
            return tx_state.Invalid(TxValidationResult::TX_CONSENSUS, "token-convert-out-zero-supply");
        }
        arith_uint256 num = arith_uint256(p.nTokenAmountBurned) * arith_uint256((uint64_t)entry.nInitialReserveLocked);
        arith_uint256 quotient = num / arith_uint256(entry.nInitialSupply);
        if (quotient > arith_uint256((uint64_t)MAX_MONEY)) {
            return tx_state.Invalid(TxValidationResult::TX_CONSENSUS, "token-convert-out-amount-overflow");
        }
        CAmount nExpectedRedemption = (CAmount)quotient.GetLow64();
        if (nExpectedRedemption <= 0 || nExpectedRedemption > reserveCoin.out.nValue) {
            return tx_state.Invalid(TxValidationResult::TX_CONSENSUS, "token-convert-out-bad-redemption-amount");
        }
        CAmount nNewReserveBalance = reserveCoin.out.nValue - nExpectedRedemption;

        // --- Validate the reserve change output (or its absence on full drain) ---
        int nNewReserveOutputs = 0;
        for (const CTxOut& out : tx.vout) {
            uint256 scriptTokenID;
            if (IsTokenReserveScript(out.scriptPubKey, &scriptTokenID) && scriptTokenID == p.tokenID) {
                nNewReserveOutputs++;
                if (out.nValue != nNewReserveBalance) {
                    return tx_state.Invalid(TxValidationResult::TX_CONSENSUS, "token-convert-out-reserve-change-mismatch");
                }
            }
        }
        if (nNewReserveBalance > 0 && nNewReserveOutputs != 1) {
            return tx_state.Invalid(TxValidationResult::TX_CONSENSUS, "token-convert-out-reserve-change-count");
        }
        if (nNewReserveBalance == 0 && nNewReserveOutputs != 0) {
            return tx_state.Invalid(TxValidationResult::TX_CONSENSUS, "token-convert-out-unexpected-reserve-output");
        }

        // --- Token-side conservation: burned tokens must come from real token-UTXO inputs ---
        uint64_t nTokenInputTotal = 0;
        for (const auto& txin : tx.vin) {
            CTokenCoin tcoin;
            if (tokenView.GetTokenCoin(txin.prevout, tcoin) && tcoin.tokenID == p.tokenID) {
                nTokenInputTotal += tcoin.nTokenAmount;
                txUndo.spentTokenCoins.push_back(std::make_pair(txin.prevout, tcoin));
                tokenView.SpendTokenCoin(txin.prevout);
            }
        }
        if (nTokenInputTotal < p.nTokenAmountBurned) {
            return tx_state.Invalid(TxValidationResult::TX_CONSENSUS, "token-convert-out-insufficient-token-input");
        }
        uint64_t nTokenChange = nTokenInputTotal - p.nTokenAmountBurned;

        uint64_t nTokenOutputTotal = 0;
        for (size_t o = 0; o < tx.vout.size(); o++) {
            const CTxOut& out = tx.vout[o];
            if (out.tokenID == p.tokenID && out.nTokenAmount > 0) {
                nTokenOutputTotal += out.nTokenAmount;
                CTokenCoin newCoin;
                newCoin.tokenID = p.tokenID;
                newCoin.nTokenAmount = out.nTokenAmount;
                newCoin.scriptPubKey = out.scriptPubKey;
                newCoin.nHeight = (uint32_t)nHeight;
                tokenView.AddTokenCoin(COutPoint(tx.GetHash(), (uint32_t)o), newCoin);
            }
        }
        if (nTokenOutputTotal != nTokenChange) {
            return tx_state.Invalid(TxValidationResult::TX_CONSENSUS, "token-convert-out-token-change-mismatch");
        }

        // --- Update registry (supply/reserve decrease) ---
        txUndo.prevRegistryState.push_back(std::make_pair(p.tokenID, entry));
        entry.nCurrentSupply -= p.nTokenAmountBurned;
        entry.nCurrentReserveLocked = nNewReserveBalance;
        tokenView.SetTokenRegistry(p.tokenID, entry);

        return true;
    }

    return true;
}
