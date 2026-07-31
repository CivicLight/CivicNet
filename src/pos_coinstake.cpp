#include <pos_coinstake.h>

bool IsCoinStakeStructure(const CTransaction& tx)
{
    // Must have at least: 1 input (kernel), 2 outputs (marker + payout)
    if (tx.vin.size() < 1) return false;
    if (tx.vout.size() < 2) return false;

    // vout[0] must be the empty marker: value 0, empty scriptPubKey
    const CTxOut& marker = tx.vout[0];
    if (marker.nValue != 0) return false;
    if (!marker.scriptPubKey.empty()) return false;

    // vout[1] must carry a positive payout (staker's balance + reward)
    if (tx.vout[1].nValue <= 0) return false;

    return true;
}
