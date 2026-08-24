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

#endif // CIVICNET_TOKEN_TOKENVALIDATION_H
