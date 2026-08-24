// Copyright (c) 2026 The CivicNet developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <rpc/server.h>
#include <rpc/util.h>
#include <validation.h>
#include <token/tokendb.h>
#include <token/tokentx.h>
#include <key_io.h>
#include <univalue.h>

static RPCHelpMan gettokeninfo()
{
    return RPCHelpMan{"gettokeninfo",
                "\nReturns metadata for a token issued on the Hybrid Value Layer.\n",
                {
                    {"tokenid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The token ID (txid of its TX_ISSUE)"},
                },
                RPCResult{
                    RPCResult::Type::OBJ, "", "",
                    {
                        {RPCResult::Type::STR_HEX, "tokenid", "the token ID"},
                        {RPCResult::Type::STR, "symbol", "the token symbol"},
                        {RPCResult::Type::STR, "name", "the token display name"},
                        {RPCResult::Type::STR, "type", "\"standard\" or \"vesting\""},
                        {RPCResult::Type::NUM, "decimals", "number of decimals"},
                        {RPCResult::Type::NUM, "initialSupply", "supply at issuance (smallest unit)"},
                        {RPCResult::Type::NUM, "currentSupply", "current circulating supply (smallest unit)"},
                        {RPCResult::Type::STR_AMOUNT, "initialReserveLocked", "CIVIC locked at issuance"},
                        {RPCResult::Type::STR_AMOUNT, "currentReserveLocked", "CIVIC currently locked in reserve"},
                        {RPCResult::Type::STR, "issuerAddress", "the issuing address"},
                        {RPCResult::Type::NUM, "issueHeight", "block height of issuance"},
                        {RPCResult::Type::STR_HEX, "issueTxid", "txid of the TX_ISSUE (same as tokenid)"},
                    }
                },
                RPCExamples{
                    HelpExampleCli("gettokeninfo", "\"a1b2c3...\"")
            + HelpExampleRpc("gettokeninfo", "\"a1b2c3...\"")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    uint256 tokenID(ParseHashV(request.params[0], "tokenid"));

    CTokenRegistryEntry entry;
    bool found;
    {
        LOCK(cs_main);
        found = ::ChainstateActive().TokenDB().ReadTokenRegistry(tokenID, entry);
    }
    if (!found) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Unknown token ID");
    }

    UniValue result(UniValue::VOBJ);
    result.pushKV("tokenid", tokenID.GetHex());
    result.pushKV("symbol", entry.symbol);
    result.pushKV("name", entry.name);
    result.pushKV("type", entry.nTokenType == TOKEN_TYPE_VESTING ? "vesting" : "standard");
    result.pushKV("decimals", (int)entry.nDecimals);
    result.pushKV("initialSupply", (uint64_t)entry.nInitialSupply);
    result.pushKV("currentSupply", (uint64_t)entry.nCurrentSupply);
    result.pushKV("initialReserveLocked", ValueFromAmount(entry.nInitialReserveLocked));
    result.pushKV("currentReserveLocked", ValueFromAmount(entry.nCurrentReserveLocked));

    CTxDestination dest;
    if (ExtractDestination(entry.issuerScriptPubKey, dest)) {
        result.pushKV("issuerAddress", EncodeDestination(dest));
    } else {
        result.pushKV("issuerAddress", "");
    }

    result.pushKV("issueHeight", (int)entry.nIssueHeight);
    result.pushKV("issueTxid", entry.issueTxid.GetHex());

    if (entry.nTokenType == TOKEN_TYPE_VESTING) {
        UniValue vesting(UniValue::VOBJ);
        vesting.pushKV("startHeight", (int)entry.nVestingStartHeight);
        vesting.pushKV("durationBlocks", (int)entry.nVestingDurationBlocks);
        vesting.pushKV("cliffBlocks", (int)entry.nVestingCliffBlocks);
        result.pushKV("vesting", vesting);
    }

    return result;
},
    };
}

void RegisterTokenRPCCommands(CRPCTable &t)
{
// clang-format off
static const CRPCCommand commands[] =
{ //  category    name              actor (function)   argNames
  //  ----------- ----------------- ------------------ ----------
    { "token",    "gettokeninfo",   &gettokeninfo,      {"tokenid"} },
};
// clang-format on
    for (const auto& c : commands) {
        t.appendCommand(c.name, &c);
    }
}
