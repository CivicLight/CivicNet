#ifndef CIVICNET_POS_COINSTAKE_H
#define CIVICNET_POS_COINSTAKE_H

#include <primitives/transaction.h>

// A coinstake transaction (the second tx in a PoS block) must have:
// - vin[0]: the staker's UTXO (kernel input)
// - vout[0]: empty (value 0, empty script) -- mandatory marker
// - vout[1]: staker's balance + block reward, paid back to staker
// - vout[2] (optional): "stake splitting" if vout[1] exceeds a threshold
//
// The nVersion PoS bit flag alone is not trusted -- consensus must also
// verify this transaction structure before accepting a block as PoS-valid.
bool IsCoinStakeStructure(const CTransaction& tx);

#endif // CIVICNET_POS_COINSTAKE_H
