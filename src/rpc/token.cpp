// Copyright (c) 2026 The CivicNet developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <rpc/server.h>
#include <rpc/util.h>
#include <core_io.h>
#include <rpc/rawtransaction_util.h>
#include <script/standard.h>
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

static RPCHelpMan createtokenissuetx()
{
    return RPCHelpMan{"createtokenissuetx",
                "\nBuilds an unsigned, unfunded raw transaction that issues a new token on\n"
                "the Hybrid Value Layer. The FIRST entry in \"inputs\" determines the token's\n"
                "ID (hash of that input's outpoint) -- it must be an input you actually\n"
                "intend to sign, since the token ID depends on it. Fund any remaining value\n"
                "with fundrawtransaction, then sign with signrawtransactionwithwallet (or\n"
                "signrawtransactionwithkey) and broadcast with sendrawtransaction -- this RPC\n"
                "never touches wallet or key material itself.\n",
                {
                    {"inputs", RPCArg::Type::ARR, RPCArg::Optional::NO, "The inputs. The first entry fixes the token ID -- must be spendable by you.",
                        {
                            {"", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                                {
                                    {"txid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The transaction id"},
                                    {"vout", RPCArg::Type::NUM, RPCArg::Optional::NO, "The output number"},
                                },
                                },
                        },
                        },
                    {"token", RPCArg::Type::OBJ, RPCArg::Optional::NO, "Token metadata",
                        {
                            {"symbol", RPCArg::Type::STR, RPCArg::Optional::NO, "Token symbol, max 12 chars, A-Z0-9"},
                            {"name", RPCArg::Type::STR, RPCArg::Optional::NO, "Token display name, max 32 chars"},
                            {"type", RPCArg::Type::STR, /* default */ "\"standard\"", "\"standard\" or \"vesting\""},
                            {"decimals", RPCArg::Type::NUM, RPCArg::Optional::NO, "Decimal places, 0-8"},
                            {"initialSupply", RPCArg::Type::NUM, RPCArg::Optional::NO, "Initial supply, smallest unit"},
                            {"reserveLockAmount", RPCArg::Type::AMOUNT, RPCArg::Optional::NO, "CIVIC to lock as reserve"},
                            {"feeBurnAmount", RPCArg::Type::AMOUNT, RPCArg::Optional::NO, "CIVIC to burn as the anti-spam issuance fee"},
                            {"mintAddress", RPCArg::Type::STR, RPCArg::Optional::NO, "Address to receive the minted initial supply"},
                            {"vestingStartHeight", RPCArg::Type::NUM, /* default */ "0", "Vesting start height (type=vesting only)"},
                            {"vestingDurationBlocks", RPCArg::Type::NUM, /* default */ "0", "Vesting duration in blocks (type=vesting only)"},
                            {"vestingCliffBlocks", RPCArg::Type::NUM, /* default */ "0", "Vesting cliff in blocks (type=vesting only)"},
                        },
                        },
                },
                RPCResult{
                    RPCResult::Type::STR_HEX, "", "the unsigned, unfunded raw transaction hex"
                },
                RPCExamples{
                    HelpExampleCli("createtokenissuetx",
                        "\"[{\\\"txid\\\":\\\"myid\\\",\\\"vout\\\":0}]\" "
                        "\"{\\\"symbol\\\":\\\"MYC\\\",\\\"name\\\":\\\"MyCoin\\\",\\\"decimals\\\":8,"
                        "\\\"initialSupply\\\":1000000,\\\"reserveLockAmount\\\":1000,"
                        "\\\"feeBurnAmount\\\":1,\\\"mintAddress\\\":\\\"myaddress\\\"}\"")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    if (request.params[0].isNull() || request.params[0].get_array().size() == 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "inputs must be a non-empty array -- the first entry fixes the token ID");
    }

    UniValue emptyOutputs(UniValue::VOBJ);
    CMutableTransaction rawTx = ConstructTransaction(request.params[0], emptyOutputs, NullUniValue, false);

    if (rawTx.vin.empty()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "inputs must be a non-empty array -- the first entry fixes the token ID");
    }
    uint256 tokenID = SerializeHash(rawTx.vin[0].prevout);

    const UniValue& tokenArg = request.params[1];
    if (!tokenArg.isObject()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "token metadata object is required");
    }

    std::string symbol = tokenArg["symbol"].get_str();
    std::string name = tokenArg["name"].get_str();
    std::string typeStr = tokenArg["type"].isNull() ? "standard" : tokenArg["type"].get_str();
    int decimals = tokenArg["decimals"].get_int();
    uint64_t initialSupply = (uint64_t)tokenArg["initialSupply"].get_int64();
    CAmount reserveLockAmount = AmountFromValue(tokenArg["reserveLockAmount"]);
    CAmount feeBurnAmount = AmountFromValue(tokenArg["feeBurnAmount"]);
    std::string mintAddressStr = tokenArg["mintAddress"].get_str();

    CTxDestination mintDest = DecodeDestination(mintAddressStr);
    if (!IsValidDestination(mintDest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid mintAddress");
    }

    bool isVesting = (typeStr == "vesting");
    if (typeStr != "standard" && typeStr != "vesting") {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "type must be \"standard\" or \"vesting\"");
    }

    CTokenIssuePayload payload;
    payload.symbol = symbol;
    payload.name = name;
    payload.nTokenType = isVesting ? TOKEN_TYPE_VESTING : TOKEN_TYPE_STANDARD;
    payload.nDecimals = (uint8_t)decimals;
    payload.nInitialSupply = initialSupply;
    payload.nFlags = 0;
    payload.poeAnchorHash.SetNull();
    if (isVesting) {
        payload.nVestingStartHeight = tokenArg["vestingStartHeight"].isNull() ? 0 : (uint32_t)tokenArg["vestingStartHeight"].get_int64();
        payload.nVestingDurationBlocks = tokenArg["vestingDurationBlocks"].isNull() ? 0 : (uint32_t)tokenArg["vestingDurationBlocks"].get_int64();
        payload.nVestingCliffBlocks = tokenArg["vestingCliffBlocks"].isNull() ? 0 : (uint32_t)tokenArg["vestingCliffBlocks"].get_int64();
    }

    // Reserve-lock output -- not spendable via any private key, see script/interpreter.cpp
    rawTx.vout.push_back(CTxOut(reserveLockAmount, BuildTokenReserveScript(tokenID)));

    // Mint output -- carries the initial supply to the issuer-chosen address
    CTxOut mintOut(0, GetScriptForDestination(mintDest));
    mintOut.tokenID = tokenID;
    mintOut.nTokenAmount = initialSupply;
    rawTx.vout.push_back(mintOut);

    // Anti-spam issuance fee -- explicit OP_RETURN burn, kept separate from miner fee for auditability
    rawTx.vout.push_back(CTxOut(feeBurnAmount, CScript() << OP_RETURN));

    rawTx.nTokenTxType = TOKEN_TX_ISSUE;
    rawTx.tokenIssuePayload = payload;

    return EncodeHexTx(CTransaction(rawTx));
},
    };
}

void RegisterTokenRPCCommands(CRPCTable &t)
{
// clang-format off
static const CRPCCommand commands[] =
{ //  category    name                  actor (function)      argNames
  //  ----------- --------------------- ---------------------- ----------
    { "token",    "gettokeninfo",       &gettokeninfo,         {"tokenid"} },
    { "token",    "createtokenissuetx", &createtokenissuetx,   {"inputs", "token"} },
};
// clang-format on
    for (const auto& c : commands) {
        t.appendCommand(c.name, &c);
    }
}
