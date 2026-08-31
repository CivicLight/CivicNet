// Copyright (c) 2026 The CivicNet developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <token/tokenvalidation.h>
#include <hash.h>
#include <arith_uint256.h>
#include <util/system.h>
#include <chainparams.h>
#include <validation.h>

int GetFeeBurnMultiplierPct()
{
    return (int)gArgs.GetArg("-burnfeemultiplier", 100);
}

uint16_t GetBurnSplitBps()
{
    return (uint16_t)gArgs.GetArg("-burnsplitbps", 2000);
}

bool ApplyTokenTx(const CTransaction& tx, CTokenViewCache& tokenView, const CCoinsViewCache& view,
                   int nHeight, std::set<unsigned int>& skipInputs,
                   CTokenBlockUndo& txUndo, TxValidationState& tx_state)
{
    if (tx.nTokenTxType == TOKEN_TX_NONE) {
        // Plain token transfer: pure conservation, no registry/reserve changes.
        // For every tokenID touched by this tx's inputs or outputs, the spent
        // token-input total must exactly equal the colored-output total --
        // no minting or burning is possible through this path.
        std::map<uint256, uint64_t> inputTotals;
        std::vector<std::pair<COutPoint, CTokenCoin>> spentThisTx;
        for (const CTxIn& txin : tx.vin) {
            CTokenCoin tcoin;
            if (tokenView.GetTokenCoin(txin.prevout, tcoin)) {
                inputTotals[tcoin.tokenID] += tcoin.nTokenAmount;
                spentThisTx.push_back(std::make_pair(txin.prevout, tcoin));
            }
        }
        std::map<uint256, uint64_t> outputTotals;
        for (const CTxOut& out : tx.vout) {
            if (out.IsTokenOutput()) {
                outputTotals[out.tokenID] += out.nTokenAmount;
            }
        }
        if (inputTotals.empty() && outputTotals.empty()) {
            // No token activity in this transaction at all -- nothing to do.
            return true;
        }
        std::set<uint256> touchedIDs;
        for (const auto& kv : inputTotals) touchedIDs.insert(kv.first);
        for (const auto& kv : outputTotals) touchedIDs.insert(kv.first);
        for (const uint256& id : touchedIDs) {
            uint64_t inAmt = inputTotals.count(id) ? inputTotals[id] : 0;
            uint64_t outAmt = outputTotals.count(id) ? outputTotals[id] : 0;

            CTokenRegistryEntry entry;
            if (!tokenView.GetTokenRegistry(id, entry)) {
                return tx_state.Invalid(TxValidationResult::TX_CONSENSUS, "token-transfer-unknown-token");
            }

            if (!entry.HasTransferFee()) {
                if (inAmt != outAmt) {
                    return tx_state.Invalid(TxValidationResult::TX_CONSENSUS, "token-transfer-unbalanced");
                }
                continue;
            }

            // --- Fee-on-transfer token: fee is computed from the total
            //     input amount moved for this tokenID in this tx (not from
            //     any single output) -- deterministic and immune to
            //     "which output is the real send vs change" ambiguity. ---
            uint16_t feeBps;
            if (entry.nFeeMode == TOKEN_FEE_MODE_RECIPIENT_CURVE) {
                feeBps = GetCurrentFeeBpsForCurve(entry.nFeeBpsStart, entry.nFeeBpsEnd,
                                                   entry.nFeeDecayDurationBlocks, entry.nIssueHeight, nHeight);
            } else if (entry.nFeeMode == TOKEN_FEE_MODE_BURN_FLAT) {
                feeBps = entry.nFeeBpsBurn;
            } else {
                return tx_state.Invalid(TxValidationResult::TX_CONSENSUS, "token-transfer-bad-fee-mode");
            }
            unsigned __int128 feeCalc = (unsigned __int128)inAmt * (unsigned __int128)feeBps / (unsigned __int128)10000;
            uint64_t fee = (uint64_t)feeCalc;

            if (entry.nFeeMode == TOKEN_FEE_MODE_RECIPIENT_CURVE) {
                // Fee is paid to feeRecipientHash160 as one of the outputs --
                // total conservation (input == output) still holds exactly,
                // the fee output is just a MANDATORY member of that total.
                if (inAmt != outAmt) {
                    return tx_state.Invalid(TxValidationResult::TX_CONSENSUS, "token-transfer-unbalanced");
                }
                if (fee > 0) {
                    bool foundFeeOutput = false;
                    for (const CTxOut& out : tx.vout) {
                        if (out.tokenID != id || out.nTokenAmount != fee) continue;
                        if (out.scriptPubKey.size() == 22 && out.scriptPubKey[0] == 0x00 && out.scriptPubKey[1] == 0x14) {
                            uint160 h(std::vector<unsigned char>(out.scriptPubKey.begin() + 2, out.scriptPubKey.end()));
                            if (h == entry.feeRecipientHash160) { foundFeeOutput = true; break; }
                        }
                    }
                    if (!foundFeeOutput) {
                        return tx_state.Invalid(TxValidationResult::TX_CONSENSUS, "token-transfer-fee-output-missing");
                    }
                }
            } else { // TOKEN_FEE_MODE_BURN_FLAT
                // Fee is destroyed -- no output for it. Conservation becomes
                // input == output + fee, with nCurrentSupply reduced by fee.
                if (inAmt != outAmt + fee) {
                    return tx_state.Invalid(TxValidationResult::TX_CONSENSUS, "token-transfer-unbalanced");
                }
                if (fee > 0) {
                    txUndo.prevRegistryState.push_back(std::make_pair(id, entry));
                    entry.nCurrentSupply -= fee;
                    tokenView.SetTokenRegistry(id, entry);
                }
            }
        }
        for (const auto& sp : spentThisTx) {
            txUndo.spentTokenCoins.push_back(sp);
            tokenView.SpendTokenCoin(sp.first, sp.second);
        }
        for (size_t i = 0; i < tx.vout.size(); i++) {
            const CTxOut& out = tx.vout[i];
            if (out.IsTokenOutput()) {
                CTokenCoin newCoin;
                newCoin.tokenID = out.tokenID;
                newCoin.nTokenAmount = out.nTokenAmount;
                newCoin.scriptPubKey = out.scriptPubKey;
                newCoin.nHeight = (uint32_t)nHeight;
                tokenView.AddTokenCoin(COutPoint(tx.GetHash(), (uint32_t)i), newCoin);
            }
        }
        return true;
    }

    if (tx.nTokenTxType == TOKEN_TX_ISSUE) {
        const CTokenIssuePayload& p = tx.tokenIssuePayload;
        // tokenID = hash of the first input's outpoint (see tx_check.cpp for why this
        // avoids the self-referential impossibility of using tx.GetHash() here).
        if (tx.vin.empty()) {
            return tx_state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-token-issue-no-inputs");
        }
        uint256 tokenID = SerializeHash(tx.vin[0].prevout);

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

        // Volume-adaptive anti-spam fee: base fee tracks the current block
        // subsidy (scaled by GetFeeBurnMultiplierPct(), capped at
        // MAX_ISSUANCE_FEE), then discounted by GetVolumeFeeTierPct() based
        // on how many TX_ISSUEs landed in the trailing VOLUME_WINDOW_BLOCKS
        // window -- heavy organic adoption isn't taxed at a flat rate. Only
        // the burned portion (GetBurnSplitBps()) is enforced here; the rest
        // becomes ordinary miner fee automatically (no explicit output for
        // that portion needed -- it's just input-minus-output remainder).
        std::vector<uint32_t> issuanceHeights = tokenView.GetIssuanceHeights();
        size_t nPruneIdx = 0;
        while (nPruneIdx < issuanceHeights.size() &&
               (int64_t)nHeight - (int64_t)issuanceHeights[nPruneIdx] > VOLUME_WINDOW_BLOCKS) {
            nPruneIdx++;
        }
        if (nPruneIdx > 0) {
            issuanceHeights.erase(issuanceHeights.begin(), issuanceHeights.begin() + nPruneIdx);
        }
        int nVolumeTierPct = GetVolumeFeeTierPct(issuanceHeights.size());
        CAmount nBlockSubsidy = GetBlockSubsidy(nHeight, Params().GetConsensus());
        CAmount nBaseFee = (nBlockSubsidy * GetFeeBurnMultiplierPct()) / 100;
        if (nBaseFee > MAX_ISSUANCE_FEE) nBaseFee = MAX_ISSUANCE_FEE;
        CAmount nTieredFee = (nBaseFee * nVolumeTierPct) / 100;
        CAmount nRequiredBurn = (nTieredFee * GetBurnSplitBps()) / 10000;

        CAmount nBurned = 0;
        for (const CTxOut& out : tx.vout) {
            if (out.scriptPubKey.IsUnspendable()) {
                nBurned += out.nValue;
            }
        }
        if (nBurned < nRequiredBurn) {
            return tx_state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-token-issue-insufficient-burn-fee");
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
        entry.issueTxid = tx.GetHash(); // the real txid of the TX_ISSUE (distinct from tokenID, used for explorer lookups)
        entry.nIssueHeight = (uint32_t)nHeight;
        entry.nVestingStartHeight = p.nVestingStartHeight;
        entry.nVestingDurationBlocks = p.nVestingDurationBlocks;
        entry.nVestingCliffBlocks = p.nVestingCliffBlocks;
        entry.nFeeMode = p.nFeeMode;
        entry.nFeeBpsStart = p.nFeeBpsStart;
        entry.nFeeBpsEnd = p.nFeeBpsEnd;
        entry.nFeeDecayDurationBlocks = p.nFeeDecayDurationBlocks;
        entry.feeRecipientHash160 = p.feeRecipientHash160;
        entry.nFeeBpsBurn = p.nFeeBpsBurn;

        if (p.IsCapped()) {
            if (p.nSupplyCap < p.nInitialSupply) {
                return tx_state.Invalid(TxValidationResult::TX_CONSENSUS, "bad-token-issue-cap-below-initial-supply");
            }
            entry.nSupplyCap = p.nSupplyCap;
            entry.nMintAuthorityExpiryHeight = p.nMintAuthorityExpiryHeight;
        }

        // Record this issuance into the rolling volume window (already
        // pruned above into the local `issuanceHeights`). Snapshot the
        // PRE-modification list into the undo record ONCE per block -- a
        // second ISSUE later in the same block must not clobber the first
        // snapshot (checked via hasPrevIssuanceHeights).
        if (!txUndo.hasPrevIssuanceHeights) {
            txUndo.prevIssuanceHeights = tokenView.GetIssuanceHeights();
            txUndo.hasPrevIssuanceHeights = true;
        }
        issuanceHeights.push_back((uint32_t)nHeight);
        tokenView.SetIssuanceHeights(issuanceHeights);

        tokenView.SetTokenRegistry(tokenID, entry);
        txUndo.newTokenIDs.push_back(tokenID);
        return true;
    }

    if (tx.nTokenTxType == TOKEN_TX_MINT) {
        const CTokenMintPayload& p = tx.tokenMintPayload;

        CTokenRegistryEntry entry;
        if (!tokenView.GetTokenRegistry(p.tokenID, entry)) {
            return tx_state.Invalid(TxValidationResult::TX_CONSENSUS, "token-mint-unknown-token");
        }
        if (!entry.IsCapped()) {
            return tx_state.Invalid(TxValidationResult::TX_CONSENSUS, "token-mint-not-capped-token");
        }
        if (entry.nMintAuthorityExpiryHeight != 0 && (uint32_t)nHeight > entry.nMintAuthorityExpiryHeight) {
            return tx_state.Invalid(TxValidationResult::TX_CONSENSUS, "token-mint-authority-expired");
        }
        if (p.nAmountToMint == 0) {
            return tx_state.Invalid(TxValidationResult::TX_CONSENSUS, "token-mint-zero-amount");
        }
        if (entry.nCurrentSupply + p.nAmountToMint < entry.nCurrentSupply ||
            entry.nCurrentSupply + p.nAmountToMint > entry.nSupplyCap) {
            return tx_state.Invalid(TxValidationResult::TX_CONSENSUS, "token-mint-exceeds-cap");
        }

        // --- Issuer authorization: one of the tx's inputs must spend from the
        //     token's issuerScriptPubKey. The input's signature is verified
        //     normally by CheckInputScripts -- this check only confirms the
        //     *correct* scriptPubKey is among the inputs; it does not itself
        //     prove authorization (that's what the signature check is for). ---
        bool fIssuerInputFound = false;
        for (const CTxIn& txin : tx.vin) {
            const Coin& coin = view.AccessCoin(txin.prevout);
            if (!coin.IsSpent() && coin.out.scriptPubKey == entry.issuerScriptPubKey) {
                fIssuerInputFound = true;
                break;
            }
        }
        if (!fIssuerInputFound) {
            return tx_state.Invalid(TxValidationResult::TX_CONSENSUS, "token-mint-not-issuer-authorized");
        }

        // --- Minted amount must land in token-colored outputs summing to exactly nAmountToMint ---
        uint64_t nMintOutputTotal = 0;
        for (size_t o = 0; o < tx.vout.size(); o++) {
            const CTxOut& out = tx.vout[o];
            if (out.tokenID == p.tokenID && out.nTokenAmount > 0) {
                nMintOutputTotal += out.nTokenAmount;
                CTokenCoin newCoin;
                newCoin.tokenID = p.tokenID;
                newCoin.nTokenAmount = out.nTokenAmount;
                newCoin.scriptPubKey = out.scriptPubKey;
                newCoin.nHeight = (uint32_t)nHeight;
                tokenView.AddTokenCoin(COutPoint(tx.GetHash(), (uint32_t)o), newCoin);
            }
        }
        if (nMintOutputTotal != p.nAmountToMint) {
            return tx_state.Invalid(TxValidationResult::TX_CONSENSUS, "token-mint-output-mismatch");
        }

        txUndo.prevRegistryState.push_back(std::make_pair(p.tokenID, entry));
        entry.nCurrentSupply += p.nAmountToMint;
        tokenView.SetTokenRegistry(p.tokenID, entry);
        return true;
    }
    if (tx.nTokenTxType == TOKEN_TX_METADATA_UPDATE) {
        const CTokenMetadataUpdatePayload& p = tx.tokenMetadataUpdatePayload;
        CTokenRegistryEntry entry;
        if (!tokenView.GetTokenRegistry(p.tokenID, entry)) {
            return tx_state.Invalid(TxValidationResult::TX_CONSENSUS, "token-metadata-unknown-token");
        }
        if (entry.fMetadataImmutable) {
            return tx_state.Invalid(TxValidationResult::TX_CONSENSUS, "token-metadata-immutable");
        }
        if (p.metadataUri.size() > MAX_METADATA_URI_LEN) {
            return tx_state.Invalid(TxValidationResult::TX_CONSENSUS, "token-metadata-uri-too-long");
        }
        // --- Issuer authorization: same pattern as TOKEN_TX_MINT -- one of the
        //     tx's inputs must spend from the token's issuerScriptPubKey. ---
        bool fIssuerInputFound = false;
        for (const CTxIn& txin : tx.vin) {
            const Coin& coin = view.AccessCoin(txin.prevout);
            if (!coin.IsSpent() && coin.out.scriptPubKey == entry.issuerScriptPubKey) {
                fIssuerInputFound = true;
                break;
            }
        }
        if (!fIssuerInputFound) {
            return tx_state.Invalid(TxValidationResult::TX_CONSENSUS, "token-metadata-not-issuer-authorized");
        }
        txUndo.prevRegistryState.push_back(std::make_pair(p.tokenID, entry));
        entry.nFlags |= TOKEN_FLAG_HAS_METADATA;
        entry.metadataUri = p.metadataUri;
        entry.metadataHash = p.metadataHash;
        if (p.fSetImmutable) {
            entry.fMetadataImmutable = true;
        }
        tokenView.SetTokenRegistry(p.tokenID, entry);
        return true;
    }

    if (tx.nTokenTxType == TOKEN_TX_BURN) {
        const CTokenBurnPayload& p = tx.tokenBurnPayload;
        CTokenRegistryEntry entry;
        if (!tokenView.GetTokenRegistry(p.tokenID, entry)) {
            return tx_state.Invalid(TxValidationResult::TX_CONSENSUS, "token-burn-unknown-token");
        }
        if (entry.nCurrentSupply < p.nAmountToBurn) {
            return tx_state.Invalid(TxValidationResult::TX_CONSENSUS, "token-burn-exceeds-supply");
        }
        // --- No authorization check of any kind beyond normal input
        //     signature verification (done by CheckInputScripts elsewhere,
        //     same as any plain transfer) -- the burner destroying their own
        //     tokens needs no one else's permission, unlike TOKEN_TX_MINT
        //     which requires matching the issuer's scriptPubKey. ---
        std::map<uint256, uint64_t> inputTotals;
        std::vector<std::pair<COutPoint, CTokenCoin>> spentThisTx;
        for (const CTxIn& txin : tx.vin) {
            CTokenCoin tcoin;
            if (tokenView.GetTokenCoin(txin.prevout, tcoin) && tcoin.tokenID == p.tokenID) {
                inputTotals[tcoin.tokenID] += tcoin.nTokenAmount;
                spentThisTx.push_back(std::make_pair(txin.prevout, tcoin));
            }
        }
        uint64_t nInputTotal = inputTotals.count(p.tokenID) ? inputTotals[p.tokenID] : 0;
        uint64_t nOutputTotal = 0;
        for (const CTxOut& out : tx.vout) {
            if (out.tokenID == p.tokenID && out.nTokenAmount > 0) {
                nOutputTotal += out.nTokenAmount;
            }
        }
        if (nInputTotal < p.nAmountToBurn || nInputTotal - nOutputTotal != p.nAmountToBurn) {
            return tx_state.Invalid(TxValidationResult::TX_CONSENSUS, "token-burn-amount-mismatch");
        }
        for (const auto& sp : spentThisTx) {
            txUndo.spentTokenCoins.push_back(sp);
            tokenView.SpendTokenCoin(sp.first, sp.second);
        }
        for (size_t o = 0; o < tx.vout.size(); o++) {
            const CTxOut& out = tx.vout[o];
            if (out.tokenID == p.tokenID && out.nTokenAmount > 0) {
                CTokenCoin newCoin;
                newCoin.tokenID = p.tokenID;
                newCoin.nTokenAmount = out.nTokenAmount;
                newCoin.scriptPubKey = out.scriptPubKey;
                newCoin.nHeight = (uint32_t)nHeight;
                tokenView.AddTokenCoin(COutPoint(tx.GetHash(), (uint32_t)o), newCoin);
            }
        }
        txUndo.prevRegistryState.push_back(std::make_pair(p.tokenID, entry));
        entry.nCurrentSupply -= p.nAmountToBurn;
        tokenView.SetTokenRegistry(p.tokenID, entry);
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
                tokenView.SpendTokenCoin(txin.prevout, tcoin);
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

    if (tx.nTokenTxType == TOKEN_TX_VESTING_RELEASE) {
        const CTokenVestingReleasePayload& p = tx.tokenVestingReleasePayload;

        if (!tokenView.HaveTokenRegistry(p.tokenID)) {
            return tx_state.Invalid(TxValidationResult::TX_CONSENSUS, "token-vesting-release-unknown-token");
        }

        // --- Locate the vesting-lock input (exactly one) and its current locked amount ---
        int nLockInputIdx = -1;
        CTokenVestingLockData vd;
        CTokenCoin lockCoin;
        for (size_t j = 0; j < tx.vin.size(); j++) {
            const Coin& coin = view.AccessCoin(tx.vin[j].prevout);
            if (coin.IsSpent()) continue;
            CTokenVestingLockData thisVd;
            if (!IsTokenVestingLockScript(coin.out.scriptPubKey, &thisVd)) continue;
            if (thisVd.tokenID != p.tokenID) continue;
            if (nLockInputIdx != -1) {
                return tx_state.Invalid(TxValidationResult::TX_CONSENSUS, "token-vesting-release-multiple-lock-inputs");
            }
            nLockInputIdx = (int)j;
            vd = thisVd;
            if (!tokenView.GetTokenCoin(tx.vin[j].prevout, lockCoin) || lockCoin.tokenID != p.tokenID) {
                return tx_state.Invalid(TxValidationResult::TX_CONSENSUS, "token-vesting-release-lock-coin-missing");
            }
        }
        if (nLockInputIdx == -1) {
            return tx_state.Invalid(TxValidationResult::TX_CONSENSUS, "token-vesting-release-missing-lock-input");
        }
        skipInputs.insert((unsigned int)nLockInputIdx);

        // --- Beneficiary authorization: another input's scriptPubKey must
        //     hash to vd.beneficiaryHash160. Signature on that input is
        //     verified normally by CheckInputScripts -- same pattern as
        //     TOKEN_TX_MINT's issuerScriptPubKey check. ---
        bool fBeneficiaryInputFound = false;
        for (size_t j = 0; j < tx.vin.size(); j++) {
            if ((int)j == nLockInputIdx) continue;
            const Coin& coin = view.AccessCoin(tx.vin[j].prevout);
            if (coin.IsSpent()) continue;
            std::vector<std::vector<unsigned char>> solutions;
            CScript::const_iterator pc = coin.out.scriptPubKey.begin();
            // Extract a plausible pubkey-hash from standard P2WPKH: 0 <20-byte-hash>.
            // Simpler and sufficient here: compare Hash160 of the whole
            // scriptPubKey's embedded witness program isn't generic enough,
            // so instead directly check via ExtractDestination + Hash160 of
            // the resulting key/script -- done at the RPC layer normally,
            // but consensus code must not depend on wallet/key_io. Compare
            // the witness program bytes directly for P2WPKH (20-byte hash
            // after OP_0), which is what all issuer/beneficiary scripts in
            // this project's RPCs produce.
            if (coin.out.scriptPubKey.size() == 22 && coin.out.scriptPubKey[0] == 0x00 && coin.out.scriptPubKey[1] == 0x14) {
                uint160 candidate(std::vector<unsigned char>(coin.out.scriptPubKey.begin() + 2, coin.out.scriptPubKey.end()));
                if (candidate == vd.beneficiaryHash160) {
                    fBeneficiaryInputFound = true;
                    break;
                }
            }
            (void)pc; (void)solutions;
        }
        if (!fBeneficiaryInputFound) {
            return tx_state.Invalid(TxValidationResult::TX_CONSENSUS, "token-vesting-release-not-beneficiary-authorized");
        }

        // --- Vesting schedule math ---
        uint64_t allowedCumulative = GetVestedCumulativeAmount(vd, nHeight);
        uint64_t alreadyReleased = vd.nOriginalTotalAmount - lockCoin.nTokenAmount;
        if (allowedCumulative < alreadyReleased) {
            return tx_state.Invalid(TxValidationResult::TX_CONSENSUS, "token-vesting-release-internal-inconsistency");
        }
        uint64_t availableNow = allowedCumulative - alreadyReleased;
        if (p.nAmountToRelease == 0 || p.nAmountToRelease > availableNow) {
            return tx_state.Invalid(TxValidationResult::TX_CONSENSUS, "token-vesting-release-exceeds-vested");
        }
        uint64_t nNewLockedAmount = lockCoin.nTokenAmount - p.nAmountToRelease;

        // --- Consume the vesting-lock coin (colored-UTXO bookkeeping, not a
        //     registry field) ---
        txUndo.spentTokenCoins.push_back(std::make_pair(tx.vin[nLockInputIdx].prevout, lockCoin));
        tokenView.SpendTokenCoin(tx.vin[nLockInputIdx].prevout, lockCoin);

        // --- Validate outputs: exactly the released amount as a plain
        //     token output, plus (if any remains) a new vesting-lock output
        //     with the SAME schedule fields and the reduced amount. ---
        uint64_t nPlainOutputTotal = 0;
        int nNewLockOutputs = 0;
        for (size_t o = 0; o < tx.vout.size(); o++) {
            const CTxOut& out = tx.vout[o];
            if (out.tokenID != p.tokenID || out.nTokenAmount == 0) continue;
            CTokenVestingLockData outVd;
            if (IsTokenVestingLockScript(out.scriptPubKey, &outVd)) {
                if (outVd.tokenID != vd.tokenID || outVd.beneficiaryHash160 != vd.beneficiaryHash160 ||
                    outVd.nStartHeight != vd.nStartHeight || outVd.nCliffBlocks != vd.nCliffBlocks ||
                    outVd.nDurationBlocks != vd.nDurationBlocks) {
                    return tx_state.Invalid(TxValidationResult::TX_CONSENSUS, "token-vesting-release-new-lock-schedule-mismatch");
                }
                if (out.nTokenAmount != nNewLockedAmount) {
                    return tx_state.Invalid(TxValidationResult::TX_CONSENSUS, "token-vesting-release-new-lock-amount-mismatch");
                }
                nNewLockOutputs++;
                CTokenCoin newLockCoin;
                newLockCoin.tokenID = p.tokenID;
                newLockCoin.nTokenAmount = out.nTokenAmount;
                newLockCoin.scriptPubKey = out.scriptPubKey;
                newLockCoin.nHeight = (uint32_t)nHeight;
                tokenView.AddTokenCoin(COutPoint(tx.GetHash(), (uint32_t)o), newLockCoin);
            } else {
                nPlainOutputTotal += out.nTokenAmount;
                CTokenCoin newCoin;
                newCoin.tokenID = p.tokenID;
                newCoin.nTokenAmount = out.nTokenAmount;
                newCoin.scriptPubKey = out.scriptPubKey;
                newCoin.nHeight = (uint32_t)nHeight;
                tokenView.AddTokenCoin(COutPoint(tx.GetHash(), (uint32_t)o), newCoin);
            }
        }
        if (nPlainOutputTotal != p.nAmountToRelease) {
            return tx_state.Invalid(TxValidationResult::TX_CONSENSUS, "token-vesting-release-plain-output-mismatch");
        }
        if (nNewLockedAmount > 0 && nNewLockOutputs != 1) {
            return tx_state.Invalid(TxValidationResult::TX_CONSENSUS, "token-vesting-release-new-lock-output-count");
        }
        if (nNewLockedAmount == 0 && nNewLockOutputs != 0) {
            return tx_state.Invalid(TxValidationResult::TX_CONSENSUS, "token-vesting-release-unexpected-new-lock");
        }

        return true;
    }

    return true;
}

bool UndoTokenBlock(const CBlock& block, CTokenViewCache& tokenView, const CTokenBlockUndo& blockUndo)
{
    // 1) Erase any token-colored outputs created by this block's transactions
    //    (ISSUE mint outputs, CONVERT_OUT token change) -- also removes their
    //    staged address-index entries via the (scriptPubKey, tokenID) carried
    //    on the CTxOut itself, since no separate coin lookup is available here.
    for (const auto& txRef : block.vtx) {
        const CTransaction& tx = *txRef;
        uint256 txid = tx.GetHash();
        for (size_t o = 0; o < tx.vout.size(); o++) {
            const CTxOut& out = tx.vout[o];
            if (!out.tokenID.IsNull() && out.nTokenAmount > 0) {
                CTokenCoin coin;
                coin.tokenID = out.tokenID;
                coin.nTokenAmount = out.nTokenAmount;
                coin.scriptPubKey = out.scriptPubKey;
                tokenView.SpendTokenCoin(COutPoint(txid, (uint32_t)o), coin);
            }
        }
    }

    // 2) Restore token-UTXO inputs that were spent during this block.
    for (const auto& kv : blockUndo.spentTokenCoins) {
        tokenView.AddTokenCoin(kv.first, kv.second);
    }

    // 3) Restore registry state, walking prevRegistryState in reverse (LIFO)
    //    so multiple touches to the same token within one block unwind correctly.
    for (auto it = blockUndo.prevRegistryState.rbegin(); it != blockUndo.prevRegistryState.rend(); ++it) {
        tokenView.SetTokenRegistry(it->first, it->second);
    }

    // 4) Erase registry entries for tokens newly created in this block.
    for (const uint256& tokenID : blockUndo.newTokenIDs) {
        tokenView.EraseTokenRegistry(tokenID);
    }
    // 5) Restore the issuance-heights volume tracker to its pre-block state,
    //    if any TX_ISSUE in this block modified it.
    if (blockUndo.hasPrevIssuanceHeights) {
        tokenView.SetIssuanceHeights(blockUndo.prevIssuanceHeights);
    }

    return true;
}
