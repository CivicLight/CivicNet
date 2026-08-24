// Copyright (c) 2026 The CivicNet developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <token/tokenvalidation.h>

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
        for (const CTxOut& out : tx.vout) {
            if (IsTokenReserveScript(out.scriptPubKey)) {
                nReserveOutputs++;
                nReserveLocked = out.nValue;
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
        // TODO: reserve redemption -- not yet implemented, next step.
        return tx_state.Invalid(TxValidationResult::TX_CONSENSUS, "token-convert-out-not-yet-implemented");
    }

    return true;
}
