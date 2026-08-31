// Copyright (c) 2026 The CivicNet developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CIVICNET_TOKEN_TOKENVALIDATION_H
#define CIVICNET_TOKEN_TOKENVALIDATION_H

#include <token/tokendb.h>
#include <consensus/validation.h>
#include <coins.h>
#include <set>

/** Applies the token-side effects of a single transaction during ConnectBlock:
 *  - TOKEN_TX_ISSUE: creates the registry entry, mint outputs already validated
 *    for sum-correctness in CheckTransaction.
 *  - TOKEN_TX_CONVERT_OUT: (not yet implemented -- next step)
 *  - TOKEN_TX_NONE with colored outputs (plain transfer): (not yet implemented)
 *
 *  On success, `skipInputs` is filled with the vin indices (if any) that
 *  should bypass normal script verification in CheckInputScripts (reserve
 *  redemption inputs), and `txUndo` captures enough state for UndoTokenTx to
 *  reverse every effect on disconnect. Returns false with `tx_state` set on
 *  any protocol violation -- callers should treat this as block-invalid,
 *  the same as any other Consensus::CheckTxInputs failure. */
bool ApplyTokenTx(const CTransaction& tx, CTokenViewCache& tokenView, const CCoinsViewCache& view,
                   int nHeight, std::set<unsigned int>& skipInputs,
                   CTokenBlockUndo& txUndo, TxValidationState& tx_state);

/** Reverses every token-side effect of a block during DisconnectBlock:
 *  erases token-colored outputs created by this block's transactions
 *  (ISSUE mints, CONVERT_OUT change), restores token-UTXO inputs that were
 *  spent, and restores registry state to how it was before this block
 *  (walking prevRegistryState in reverse so multiple touches to the same
 *  token within one block unwind correctly). */
bool UndoTokenBlock(const CBlock& block, CTokenViewCache& tokenView, const CTokenBlockUndo& blockUndo);

// --- TOKEN_TX_ISSUE anti-spam fee configuration, read from civicnet.conf
// (or -burnfeemultiplier/-burnsplitbps CLI args) so operators can adjust
// without a rebuild -- still requires a coordinated, manually-synchronized
// restart across all nodes, same as any other consensus-affecting value,
// just without the compile step. Integer percent/bps units throughout
// (no floating point), matching this codebase's existing convention. ---

//! Percentage multiplier applied to the current block subsidy to derive the
//! base issuance fee (100 = 1.0x, i.e. fee tracks the block reward 1:1 by
//! default). Read from "-burnfeemultiplier" (default 100).
int GetFeeBurnMultiplierPct();

//! Basis points of the (tiered) issuance fee that gets burned outright; the
//! remainder becomes ordinary miner fee (no explicit output needed for that
//! portion -- it's just the tx's implicit input-minus-output remainder).
//! Read from "-burnsplitbps" (default 2000 = 20% burned, 80% to miner).
uint16_t GetBurnSplitBps();

#endif // CIVICNET_TOKEN_TOKENVALIDATION_H
