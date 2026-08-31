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
#include <token/tokenvalidation.h>
#include <chainparams.h>
#include <key_io.h>
#include <univalue.h>
#include <map>

static UniValue TokenRegistryEntryToUniValue(const uint256& tokenID, const CTokenRegistryEntry& entry)
{
    UniValue result(UniValue::VOBJ);
    result.pushKV("tokenid", tokenID.GetHex());
    result.pushKV("symbol", entry.symbol);
    result.pushKV("name", entry.name);
    result.pushKV("type", entry.nTokenType == TOKEN_TYPE_VESTING ? "vesting" : "standard");
    result.pushKV("capped", entry.IsCapped());
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
    if (entry.HasMetadata()) {
        UniValue metadata(UniValue::VOBJ);
        metadata.pushKV("uri", entry.metadataUri);
        metadata.pushKV("hash", entry.metadataHash.GetHex());
        metadata.pushKV("immutable", entry.fMetadataImmutable);
        result.pushKV("metadata", metadata);
    }

    if (entry.nTokenType == TOKEN_TYPE_VESTING) {
        UniValue vesting(UniValue::VOBJ);
        vesting.pushKV("startHeight", (int)entry.nVestingStartHeight);
        vesting.pushKV("durationBlocks", (int)entry.nVestingDurationBlocks);
        vesting.pushKV("cliffBlocks", (int)entry.nVestingCliffBlocks);
        result.pushKV("vesting", vesting);
    }
    if (entry.IsCapped()) {
        result.pushKV("supplyCap", (uint64_t)entry.nSupplyCap);
        if (entry.nMintAuthorityExpiryHeight != 0) {
            result.pushKV("mintAuthorityExpiryHeight", (int)entry.nMintAuthorityExpiryHeight);
        } else {
            result.pushKV("mintAuthorityExpiryHeight", UniValue::VNULL);
        }
    }
    if (entry.HasTransferFee()) {
        UniValue fee(UniValue::VOBJ);
        if (entry.nFeeMode == TOKEN_FEE_MODE_RECIPIENT_CURVE) {
            fee.pushKV("mode", "recipient_curve");
            fee.pushKV("feeBpsStart", (int)entry.nFeeBpsStart);
            fee.pushKV("feeBpsEnd", (int)entry.nFeeBpsEnd);
            fee.pushKV("decayDurationBlocks", (int)entry.nFeeDecayDurationBlocks);
            CScript recipientScript;
            recipientScript << OP_0 << std::vector<unsigned char>(entry.feeRecipientHash160.begin(), entry.feeRecipientHash160.end());
            CTxDestination recipDest;
            if (ExtractDestination(recipientScript, recipDest)) {
                fee.pushKV("feeRecipientAddress", EncodeDestination(recipDest));
            }
        } else if (entry.nFeeMode == TOKEN_FEE_MODE_BURN_FLAT) {
            fee.pushKV("mode", "burn_flat");
            fee.pushKV("feeBpsBurn", (int)entry.nFeeBpsBurn);
        }
        result.pushKV("transferFee", fee);
    }
    return result;
}

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
                        {RPCResult::Type::BOOL, "capped", "true if this token has an issuer mint authority (supply NOT permanently fixed); false means supply has been fixed since issuance and can never change"},
                        {RPCResult::Type::NUM, "supplyCap", /* optional */ true, "hard ceiling on total supply, smallest unit (present only if capped)"},
                        {RPCResult::Type::NUM, "mintAuthorityExpiryHeight", /* optional */ true, "block height after which minting is no longer possible, or null for no expiry (present only if capped)"},
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

    return TokenRegistryEntryToUniValue(tokenID, entry);
},
    };
}

static RPCHelpMan listtokens()
{
    return RPCHelpMan{"listtokens",
                "\nLists metadata for every token issued on the Hybrid Value Layer.\n",
                {},
                RPCResult{
                    RPCResult::Type::ARR, "", "",
                    {
                        {RPCResult::Type::OBJ, "", "", {
                            {RPCResult::Type::STR_HEX, "tokenid", "the token ID"},
                            {RPCResult::Type::STR, "symbol", "the token symbol"},
                        }},
                    }
                },
                RPCExamples{
                    HelpExampleCli("listtokens", "")
            + HelpExampleRpc("listtokens", "")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    std::vector<std::pair<uint256, CTokenRegistryEntry>> entries;
    {
        LOCK(cs_main);
        ::ChainstateActive().TokenDB().GetAllTokens(entries);
    }

    UniValue result(UniValue::VARR);
    for (const auto& kv : entries) {
        result.push_back(TokenRegistryEntryToUniValue(kv.first, kv.second));
    }
    return result;
},
    };
}

static RPCHelpMan gettokenbalance()
{
    return RPCHelpMan{"gettokenbalance",
                "\nReturns the token balance(s) held at an address. If tokenid is given,\n"
                "returns a single number. Otherwise returns balances for every token held\n"
                "at that address.\n",
                {
                    {"address", RPCArg::Type::STR, RPCArg::Optional::NO, "The address to check"},
                    {"tokenid", RPCArg::Type::STR_HEX, RPCArg::Optional::OMITTED, "If given, restrict to this token"},
                },
                RPCResult{
                    RPCResult::Type::ARR, "", "one entry per token held (a single entry if tokenid was given)",
                    {
                        {RPCResult::Type::OBJ, "", "", {
                            {RPCResult::Type::STR_HEX, "tokenid", "the token ID"},
                            {RPCResult::Type::STR, "symbol", /* optional */ true, "the token symbol, if known"},
                            {RPCResult::Type::NUM, "balance", "the balance, smallest unit"},
                        }},
                    }
                },
                RPCExamples{
                    HelpExampleCli("gettokenbalance", "\"myaddress\"")
            + HelpExampleCli("gettokenbalance", "\"myaddress\" \"a1b2c3...\"")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    CTxDestination dest = DecodeDestination(request.params[0].get_str());
    if (!IsValidDestination(dest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
    }
    CScript scriptPubKey = GetScriptForDestination(dest);

    LOCK(cs_main);
    CTokenDB& tokenDB = ::ChainstateActive().TokenDB();

    if (!request.params[1].isNull()) {
        uint256 tokenID(ParseHashV(request.params[1], "tokenid"));
        std::vector<COutPoint> outpoints = tokenDB.GetTokenOutpointsForAddress(scriptPubKey, tokenID);
        uint64_t total = 0;
        for (const COutPoint& op : outpoints) {
            CTokenCoin coin;
            if (tokenDB.ReadTokenCoin(op, coin)) total += coin.nTokenAmount;
        }
        UniValue result(UniValue::VARR);
        UniValue item(UniValue::VOBJ);
        item.pushKV("tokenid", tokenID.GetHex());
        CTokenRegistryEntry entry;
        if (tokenDB.ReadTokenRegistry(tokenID, entry)) {
            item.pushKV("symbol", entry.symbol);
        }
        item.pushKV("balance", (uint64_t)total);
        result.push_back(item);
        return result;
    }

    std::vector<std::pair<uint256, COutPoint>> all = tokenDB.GetAllTokenOutpointsForAddress(scriptPubKey);
    std::map<uint256, uint64_t> balances;
    for (const auto& kv : all) {
        CTokenCoin coin;
        if (tokenDB.ReadTokenCoin(kv.second, coin)) balances[kv.first] += coin.nTokenAmount;
    }

    UniValue result(UniValue::VARR);
    for (const auto& kv : balances) {
        CTokenRegistryEntry entry;
        UniValue item(UniValue::VOBJ);
        item.pushKV("tokenid", kv.first.GetHex());
        if (tokenDB.ReadTokenRegistry(kv.first, entry)) {
            item.pushKV("symbol", entry.symbol);
        }
        item.pushKV("balance", (uint64_t)kv.second);
        result.push_back(item);
    }
    return result;
},
    };
}

static RPCHelpMan listtokenunspent()
{
    return RPCHelpMan{"listtokenunspent",
                "\nReturns token-colored unspent outputs, optionally filtered by tokenid\n"
                "and/or address. With no filters at all, this scans every token UTXO in\n"
                "tokendb/ -- fine at current scale.\n",
                {
                    {"tokenid", RPCArg::Type::STR_HEX, RPCArg::Optional::OMITTED, "Filter by token ID"},
                    {"address", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "Filter by address"},
                },
                RPCResult{
                    RPCResult::Type::ARR, "", "",
                    {
                        {RPCResult::Type::OBJ, "", "", {
                            {RPCResult::Type::STR_HEX, "txid", "the transaction id"},
                            {RPCResult::Type::NUM, "vout", "the output index"},
                            {RPCResult::Type::STR_HEX, "tokenid", "the token ID"},
                            {RPCResult::Type::NUM, "amount", "the token amount, smallest unit"},
                            {RPCResult::Type::STR, "address", "the holding address"},
                            {RPCResult::Type::NUM, "confirmations", "confirmations"},
                        }},
                    }
                },
                RPCExamples{
                    HelpExampleCli("listtokenunspent", "")
            + HelpExampleCli("listtokenunspent", "\"a1b2c3...\" \"myaddress\"")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    bool hasTokenFilter = !request.params[0].isNull();
    bool hasAddressFilter = !request.params[1].isNull();

    uint256 tokenFilter;
    if (hasTokenFilter) tokenFilter = ParseHashV(request.params[0], "tokenid");

    CScript addressFilterScript;
    if (hasAddressFilter) {
        CTxDestination dest = DecodeDestination(request.params[1].get_str());
        if (!IsValidDestination(dest)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
        }
        addressFilterScript = GetScriptForDestination(dest);
    }

    LOCK(cs_main);
    CTokenDB& tokenDB = ::ChainstateActive().TokenDB();
    int nHeight = ::ChainActive().Height();

    std::vector<std::pair<COutPoint, CTokenCoin>> coins;

    if (hasAddressFilter) {
        std::vector<COutPoint> outpoints;
        if (hasTokenFilter) {
            outpoints = tokenDB.GetTokenOutpointsForAddress(addressFilterScript, tokenFilter);
        } else {
            for (const auto& kv : tokenDB.GetAllTokenOutpointsForAddress(addressFilterScript)) {
                outpoints.push_back(kv.second);
            }
        }
        for (const COutPoint& op : outpoints) {
            CTokenCoin coin;
            if (tokenDB.ReadTokenCoin(op, coin)) coins.emplace_back(op, coin);
        }
    } else {
        std::vector<std::pair<COutPoint, CTokenCoin>> all;
        tokenDB.GetAllTokenCoins(all);
        for (auto& kv : all) {
            if (hasTokenFilter && kv.second.tokenID != tokenFilter) continue;
            coins.push_back(kv);
        }
    }

    UniValue result(UniValue::VARR);
    for (const auto& kv : coins) {
        const COutPoint& op = kv.first;
        const CTokenCoin& coin = kv.second;
        UniValue item(UniValue::VOBJ);
        item.pushKV("txid", op.hash.GetHex());
        item.pushKV("vout", (int)op.n);
        item.pushKV("tokenid", coin.tokenID.GetHex());
        item.pushKV("amount", (uint64_t)coin.nTokenAmount);
        CTxDestination dest;
        if (ExtractDestination(coin.scriptPubKey, dest)) {
            item.pushKV("address", EncodeDestination(dest));
        } else {
            item.pushKV("address", "");
        }
        item.pushKV("confirmations", coin.nHeight > 0 ? (nHeight - (int)coin.nHeight + 1) : 0);
        result.push_back(item);
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
                            {"type", RPCArg::Type::STR, /* default */ "\"standard\"", "\"standard\" or \"vesting\" -- the supply distribution mechanism"},
                            {"decimals", RPCArg::Type::NUM, RPCArg::Optional::NO, "Decimal places, 0-8"},
                            {"initialSupply", RPCArg::Type::NUM, RPCArg::Optional::NO, "Initial supply, smallest unit"},
                            {"reserveLockAmount", RPCArg::Type::AMOUNT, RPCArg::Optional::NO, "CIVIC to lock as reserve"},
                            {"feeBurnAmount", RPCArg::Type::AMOUNT, RPCArg::Optional::OMITTED, "CIVIC to burn as the anti-spam issuance fee. If omitted, auto-computed from the current block subsidy, recent issuance volume, and the network's burn-fee/split settings -- the auto-computed value is a snapshot at broadcast time and may differ slightly from what's required by the time this tx actually confirms; consensus is authoritative, this is a convenience estimate."},
                            {"mintAddress", RPCArg::Type::STR, RPCArg::Optional::NO, "Address to receive the minted initial supply"},
                            {"vestingStartHeight", RPCArg::Type::NUM, /* default */ "0", "Vesting start height (type=vesting only)"},
                            {"vestingDurationBlocks", RPCArg::Type::NUM, /* default */ "0", "Vesting duration in blocks (type=vesting only)"},
                            {"vestingCliffBlocks", RPCArg::Type::NUM, /* default */ "0", "Vesting cliff in blocks (type=vesting only)"},
                            {"capped", RPCArg::Type::BOOL, /* default */ "false", "If true, an issuer-authorized mint authority is created for this token, letting supply grow later via createtokenminttx, up to supplyCap. If false (default), supply is permanently fixed at initialSupply from issuance -- this absence is the trust signal holders can check via gettokeninfo. Orthogonal to type -- a vesting token can also be capped."},
                            {"supplyCap", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "Hard ceiling on total supply, smallest unit (required if capped=true, must be >= initialSupply)"},
                            {"mintAuthorityExpiryHeight", RPCArg::Type::NUM, /* default */ "0", "Block height after which no further minting is possible, even below supplyCap (capped only). 0 = no expiry."},
                            {"feeMode", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "\"recipient_curve\" or \"burn_flat\" -- enables fee-on-transfer, permanently fixed at issuance (no update mechanism exists for either mode, by design)."},
                            {"feeBpsStart", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "Fee in basis points (1 = 0.01%) at issuance, decaying to feeBpsEnd (feeMode=recipient_curve only)"},
                            {"feeBpsEnd", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "Fee in basis points after the decay period, held flat forever after (feeMode=recipient_curve only)"},
                            {"feeDecayDurationBlocks", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "Blocks over which the fee decays from feeBpsStart to feeBpsEnd, linearly. 0 = flat feeBpsEnd from issuance (feeMode=recipient_curve only)"},
                            {"feeRecipientAddress", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "Address that receives the fee on every transfer (feeMode=recipient_curve only)"},
                            {"feeBpsBurn", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "Fee in basis points, burned (destroyed, not sent anywhere) on every transfer (feeMode=burn_flat only)"},
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
    CAmount feeBurnAmount;
    if (tokenArg["feeBurnAmount"].isNull()) {
        // Auto-compute the currently-required anti-spam fee: base fee
        // tracks the block subsidy (capped at MAX_ISSUANCE_FEE), discounted
        // by recent issuance volume, of which GetBurnSplitBps() gets burned
        // (the rest becomes ordinary miner fee, no output needed for it).
        // Mirrors the exact formula enforced in ApplyTokenTx's TOKEN_TX_ISSUE
        // case -- see tokenvalidation.cpp for the authoritative version.
        int nEstHeight;
        std::vector<uint32_t> issuanceHeights;
        {
            LOCK(cs_main);
            nEstHeight = ::ChainActive().Height() + 1;
            ::ChainstateActive().TokenDB().ReadIssuanceHeights(issuanceHeights);
        }
        size_t nPruneIdx = 0;
        while (nPruneIdx < issuanceHeights.size() &&
               (int64_t)nEstHeight - (int64_t)issuanceHeights[nPruneIdx] > VOLUME_WINDOW_BLOCKS) {
            nPruneIdx++;
        }
        size_t volumeCount = issuanceHeights.size() - nPruneIdx;
        int tierPct = GetVolumeFeeTierPct(volumeCount);
        CAmount blockSubsidy = GetBlockSubsidy(nEstHeight, Params().GetConsensus());
        CAmount baseFee = (blockSubsidy * GetFeeBurnMultiplierPct()) / 100;
        if (baseFee > MAX_ISSUANCE_FEE) baseFee = MAX_ISSUANCE_FEE;
        CAmount tieredFee = (baseFee * tierPct) / 100;
        feeBurnAmount = (tieredFee * GetBurnSplitBps()) / 10000;
    } else {
        feeBurnAmount = AmountFromValue(tokenArg["feeBurnAmount"]);
    }
    std::string mintAddressStr = tokenArg["mintAddress"].get_str();

    CTxDestination mintDest = DecodeDestination(mintAddressStr);
    if (!IsValidDestination(mintDest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid mintAddress");
    }

    bool isVesting = (typeStr == "vesting");
    if (typeStr != "standard" && typeStr != "vesting") {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "type must be \"standard\" or \"vesting\"");
    }
    bool isCapped = tokenArg["capped"].isBool() && tokenArg["capped"].get_bool();

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
    if (isCapped) {
        if (tokenArg["supplyCap"].isNull()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "supplyCap is required when capped=true");
        }
        payload.nFlags |= TOKEN_FLAG_CAPPED;
        payload.nSupplyCap = (uint64_t)tokenArg["supplyCap"].get_int64();
        if (payload.nSupplyCap < payload.nInitialSupply) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "supplyCap must be >= initialSupply");
        }
        payload.nMintAuthorityExpiryHeight = tokenArg["mintAuthorityExpiryHeight"].isNull() ? 0 : (uint32_t)tokenArg["mintAuthorityExpiryHeight"].get_int64();
    }

    if (!tokenArg["feeMode"].isNull()) {
        std::string feeModeStr = tokenArg["feeMode"].get_str();
        if (feeModeStr == "recipient_curve") {
            payload.nFlags |= TOKEN_FLAG_TRANSFER_FEE;
            payload.nFeeMode = TOKEN_FEE_MODE_RECIPIENT_CURVE;
            if (tokenArg["feeBpsStart"].isNull() || tokenArg["feeBpsEnd"].isNull() || tokenArg["feeRecipientAddress"].isNull()) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "feeBpsStart, feeBpsEnd, and feeRecipientAddress are required when feeMode=recipient_curve");
            }
            int64_t bpsStart = tokenArg["feeBpsStart"].get_int64();
            int64_t bpsEnd = tokenArg["feeBpsEnd"].get_int64();
            if (bpsStart < 0 || bpsStart > 10000 || bpsEnd < 0 || bpsEnd > 10000) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "feeBpsStart/feeBpsEnd must be between 0 and 10000");
            }
            payload.nFeeBpsStart = (uint16_t)bpsStart;
            payload.nFeeBpsEnd = (uint16_t)bpsEnd;
            payload.nFeeDecayDurationBlocks = tokenArg["feeDecayDurationBlocks"].isNull() ? 0 : (uint32_t)tokenArg["feeDecayDurationBlocks"].get_int64();
            CTxDestination feeRecipDest = DecodeDestination(tokenArg["feeRecipientAddress"].get_str());
            if (!IsValidDestination(feeRecipDest)) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid feeRecipientAddress");
            }
            CScript feeRecipScript = GetScriptForDestination(feeRecipDest);
            if (feeRecipScript.size() != 22 || feeRecipScript[0] != 0x00 || feeRecipScript[1] != 0x14) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "feeRecipientAddress must be a P2WPKH (bech32) address");
            }
            payload.feeRecipientHash160 = uint160(std::vector<unsigned char>(feeRecipScript.begin() + 2, feeRecipScript.end()));
        } else if (feeModeStr == "burn_flat") {
            payload.nFlags |= TOKEN_FLAG_TRANSFER_FEE;
            payload.nFeeMode = TOKEN_FEE_MODE_BURN_FLAT;
            if (tokenArg["feeBpsBurn"].isNull()) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "feeBpsBurn is required when feeMode=burn_flat");
            }
            int64_t bpsBurn = tokenArg["feeBpsBurn"].get_int64();
            if (bpsBurn < 0 || bpsBurn > 10000) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "feeBpsBurn must be between 0 and 10000");
            }
            payload.nFeeBpsBurn = (uint16_t)bpsBurn;
        } else {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "feeMode must be \"recipient_curve\" or \"burn_flat\"");
        }
    }

    rawTx.vout.push_back(CTxOut(reserveLockAmount, BuildTokenReserveScript(tokenID)));

    CTxOut mintOut;
    if (isVesting) {
        CScript mintDestScript = GetScriptForDestination(mintDest);
        if (mintDestScript.size() != 22 || mintDestScript[0] != 0x00 || mintDestScript[1] != 0x14) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "mintAddress must be a P2WPKH (bech32) address for a vesting token");
        }
        CTokenVestingLockData vd;
        vd.tokenID = tokenID;
        vd.beneficiaryHash160 = uint160(std::vector<unsigned char>(mintDestScript.begin() + 2, mintDestScript.end()));
        vd.nStartHeight = payload.nVestingStartHeight;
        vd.nCliffBlocks = payload.nVestingCliffBlocks;
        vd.nDurationBlocks = payload.nVestingDurationBlocks;
        vd.nOriginalTotalAmount = initialSupply;
        mintOut = CTxOut(0, BuildTokenVestingLockScript(vd));
    } else {
        mintOut = CTxOut(0, GetScriptForDestination(mintDest));
    }
    mintOut.tokenID = tokenID;
    mintOut.nTokenAmount = initialSupply;
    rawTx.vout.push_back(mintOut);

    rawTx.vout.push_back(CTxOut(feeBurnAmount, CScript() << OP_RETURN));

    rawTx.nTokenTxType = TOKEN_TX_ISSUE;
    rawTx.tokenIssuePayload = payload;

    return EncodeHexTx(CTransaction(rawTx));
},
    };
}

static RPCHelpMan createtokenconverttx()
{
    return RPCHelpMan{"createtokenconverttx",
                "\nBuilds an unsigned, unfunded raw transaction that redeems (burns) token\n"
                "supply for its proportional share of the locked CIVIC reserve. \"inputs\"\n"
                "must contain exactly one reserve-lock UTXO for this token plus one or more\n"
                "token-carrying UTXOs for this token -- which is which is detected\n"
                "automatically from chain state. Fund any remaining fee with\n"
                "fundrawtransaction, then sign and broadcast -- this RPC never touches\n"
                "wallet or key material itself.\n",
                {
                    {"inputs", RPCArg::Type::ARR, RPCArg::Optional::NO, "The reserve-lock UTXO and token-carrying UTXO(s) being redeemed.",
                        {
                            {"", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                                {
                                    {"txid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The transaction id"},
                                    {"vout", RPCArg::Type::NUM, RPCArg::Optional::NO, "The output number"},
                                },
                                },
                        },
                        },
                    {"token", RPCArg::Type::OBJ, RPCArg::Optional::NO, "Redemption details",
                        {
                            {"tokenid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The token ID"},
                            {"amountToBurn", RPCArg::Type::NUM, RPCArg::Optional::NO, "Token amount to burn, smallest unit"},
                            {"redemptionAddress", RPCArg::Type::STR, RPCArg::Optional::NO, "Address to receive the redeemed CIVIC"},
                            {"tokenChangeAddress", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "Address for leftover token change, required if the token inputs exceed amountToBurn"},
                            {"feeAmount", RPCArg::Type::AMOUNT, RPCArg::Optional::OMITTED, "Miner fee, required if a plain CIVIC funding UTXO is included in inputs (the reserve-lock input cannot be sized by the wallet, so fundrawtransaction cannot be used for this tx type -- fund manually)"},
                            {"changeAddress", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "Address for leftover CIVIC change from the funding input, required if funding exceeds feeAmount"},
                        },
                        },
                },
                RPCResult{
                    RPCResult::Type::STR_HEX, "", "the unsigned, unfunded raw transaction hex"
                },
                RPCExamples{
                    HelpExampleCli("createtokenconverttx",
                        "\"[{\\\"txid\\\":\\\"reserveid\\\",\\\"vout\\\":0},{\\\"txid\\\":\\\"tokenid\\\",\\\"vout\\\":1}]\" "
                        "\"{\\\"tokenid\\\":\\\"a1b2c3...\\\",\\\"amountToBurn\\\":500000,"
                        "\\\"redemptionAddress\\\":\\\"myaddress\\\"}\"")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    if (request.params[0].isNull() || request.params[0].get_array().size() == 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "inputs must be a non-empty array");
    }

    UniValue emptyOutputs(UniValue::VOBJ);
    CMutableTransaction rawTx = ConstructTransaction(request.params[0], emptyOutputs, NullUniValue, false);

    const UniValue& tokenArg = request.params[1];
    if (!tokenArg.isObject()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "token object is required");
    }

    uint256 tokenID(ParseHashV(tokenArg["tokenid"], "tokenid"));
    uint64_t amountToBurn = (uint64_t)tokenArg["amountToBurn"].get_int64();
    std::string redemptionAddressStr = tokenArg["redemptionAddress"].get_str();
    CTxDestination redemptionDest = DecodeDestination(redemptionAddressStr);
    if (!IsValidDestination(redemptionDest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid redemptionAddress");
    }

    CTokenRegistryEntry entry;
    CAmount nReserveValue = -1;
    uint64_t nTokenInputTotal = 0;
    CAmount nFundingTotal = 0;
    {
        LOCK(cs_main);
        if (!::ChainstateActive().TokenDB().ReadTokenRegistry(tokenID, entry)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Unknown token ID");
        }
        for (const CTxIn& txin : rawTx.vin) {
            const Coin& coin = ::ChainstateActive().CoinsTip().AccessCoin(txin.prevout);
            uint256 scriptTokenID;
            if (!coin.IsSpent() && IsTokenReserveScript(coin.out.scriptPubKey, &scriptTokenID) && scriptTokenID == tokenID) {
                if (nReserveValue != -1) {
                    throw JSONRPCError(RPC_INVALID_PARAMETER, "inputs contains more than one reserve-lock UTXO for this token");
                }
                nReserveValue = coin.out.nValue;
                continue;
            }
            CTokenCoin tcoin;
            if (::ChainstateActive().TokenDB().ReadTokenCoin(txin.prevout, tcoin) && tcoin.tokenID == tokenID) {
                nTokenInputTotal += tcoin.nTokenAmount;
                continue;
            }
            if (coin.IsSpent()) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "input is spent: " + txin.prevout.ToString());
            }
            nFundingTotal += coin.out.nValue;
        }
    }
    if (nReserveValue == -1) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "inputs must include the token's current reserve-lock UTXO");
    }
    if (nTokenInputTotal < amountToBurn) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "token inputs total less than amountToBurn");
    }

    if (entry.nInitialSupply == 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "token has zero initial supply");
    }
    arith_uint256 num = arith_uint256(amountToBurn) * arith_uint256((uint64_t)entry.nInitialReserveLocked);
    arith_uint256 quotient = num / arith_uint256(entry.nInitialSupply);
    if (quotient > arith_uint256((uint64_t)MAX_MONEY)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "redemption amount overflow");
    }
    CAmount nExpectedRedemption = (CAmount)quotient.GetLow64();
    if (nExpectedRedemption <= 0 || nExpectedRedemption > nReserveValue) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "computed redemption amount is invalid for the current reserve balance");
    }
    CAmount nNewReserveBalance = nReserveValue - nExpectedRedemption;
    uint64_t nTokenChange = nTokenInputTotal - amountToBurn;

    rawTx.vout.push_back(CTxOut(nExpectedRedemption, GetScriptForDestination(redemptionDest)));

    if (nNewReserveBalance > 0) {
        rawTx.vout.push_back(CTxOut(nNewReserveBalance, BuildTokenReserveScript(tokenID)));
    }

    if (nTokenChange > 0) {
        if (tokenArg["tokenChangeAddress"].isNull()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "tokenChangeAddress is required: token inputs exceed amountToBurn");
        }
        CTxDestination changeDest = DecodeDestination(tokenArg["tokenChangeAddress"].get_str());
        if (!IsValidDestination(changeDest)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid tokenChangeAddress");
        }
        CTxOut changeOut(0, GetScriptForDestination(changeDest));
        changeOut.tokenID = tokenID;
        changeOut.nTokenAmount = nTokenChange;
        rawTx.vout.push_back(changeOut);
    }

    if (nFundingTotal > 0) {
        if (tokenArg["feeAmount"].isNull()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "feeAmount is required when a plain CIVIC funding input is included");
        }
        CAmount nFeeAmount = AmountFromValue(tokenArg["feeAmount"]);
        if (nFeeAmount <= 0 || nFeeAmount > nFundingTotal) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "feeAmount must be positive and not exceed the funding input total");
        }
        CAmount nChange = nFundingTotal - nFeeAmount;
        if (nChange > 0) {
            if (tokenArg["changeAddress"].isNull()) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "changeAddress is required: funding input exceeds feeAmount");
            }
            CTxDestination fundingChangeDest = DecodeDestination(tokenArg["changeAddress"].get_str());
            if (!IsValidDestination(fundingChangeDest)) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid changeAddress");
            }
            rawTx.vout.push_back(CTxOut(nChange, GetScriptForDestination(fundingChangeDest)));
        }
    }

    rawTx.nTokenTxType = TOKEN_TX_CONVERT_OUT;
    rawTx.tokenConvertOutPayload.tokenID = tokenID;
    rawTx.tokenConvertOutPayload.nTokenAmountBurned = amountToBurn;

    return EncodeHexTx(CTransaction(rawTx));
},
    };
}

static RPCHelpMan createtokenminttx()
{
    return RPCHelpMan{"createtokenminttx",
                "\nBuilds an unsigned, unfunded raw transaction that mints additional supply\n"
                "for a token issued with capped=true, up to its supplyCap. \"inputs\" must include\n"
                "at least one UTXO spendable by the token's issuer address -- consensus\n"
                "verifies this via the input's signature (CheckInputScripts) plus a\n"
                "scriptPubKey match against the token's registered issuer, so any input\n"
                "you don't control simply makes the tx unsignable/invalid, not a security\n"
                "hole. Fund any remaining fee with fundrawtransaction (all inputs here are\n"
                "normal spendable coins, unlike createtokenconverttx's reserve-lock input,\n"
                "so fundrawtransaction works normally), then sign and broadcast.\n",
                {
                    {"inputs", RPCArg::Type::ARR, RPCArg::Optional::NO, "Inputs, at least one of which must be spendable by the token's issuer address.",
                        {
                            {"", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                                {
                                    {"txid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The transaction id"},
                                    {"vout", RPCArg::Type::NUM, RPCArg::Optional::NO, "The output number"},
                                },
                                },
                        },
                        },
                    {"token", RPCArg::Type::OBJ, RPCArg::Optional::NO, "Mint details",
                        {
                            {"tokenid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The token ID"},
                            {"amountToMint", RPCArg::Type::NUM, RPCArg::Optional::NO, "Additional supply to mint, smallest unit"},
                            {"mintAddress", RPCArg::Type::STR, RPCArg::Optional::NO, "Address to receive the newly minted supply"},
                        },
                        },
                },
                RPCResult{
                    RPCResult::Type::STR_HEX, "", "the unsigned, unfunded raw transaction hex"
                },
                RPCExamples{
                    HelpExampleCli("createtokenminttx",
                        "\"[{\\\"txid\\\":\\\"issuerutxo\\\",\\\"vout\\\":0}]\" "
                        "\"{\\\"tokenid\\\":\\\"a1b2c3...\\\",\\\"amountToMint\\\":250000,"
                        "\\\"mintAddress\\\":\\\"myaddress\\\"}\"")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    if (request.params[0].isNull() || request.params[0].get_array().size() == 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "inputs must be a non-empty array");
    }

    UniValue emptyOutputs(UniValue::VOBJ);
    CMutableTransaction rawTx = ConstructTransaction(request.params[0], emptyOutputs, NullUniValue, false);

    const UniValue& tokenArg = request.params[1];
    if (!tokenArg.isObject()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "token object is required");
    }

    uint256 tokenID(ParseHashV(tokenArg["tokenid"], "tokenid"));
    uint64_t amountToMint = (uint64_t)tokenArg["amountToMint"].get_int64();
    if (amountToMint == 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "amountToMint must be positive");
    }
    std::string mintAddressStr = tokenArg["mintAddress"].get_str();
    CTxDestination mintDest = DecodeDestination(mintAddressStr);
    if (!IsValidDestination(mintDest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid mintAddress");
    }

    {
        LOCK(cs_main);
        CTokenRegistryEntry entry;
        if (!::ChainstateActive().TokenDB().ReadTokenRegistry(tokenID, entry)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Unknown token ID");
        }
        if (!entry.IsCapped()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "token has no mint authority (issued without capped=true) -- minting additional supply is not permitted");
        }
        int nextHeight;
        {
            LOCK(cs_main);
            nextHeight = ::ChainActive().Height() + 1;
        }
        if (entry.nMintAuthorityExpiryHeight != 0 && (uint32_t)nextHeight > entry.nMintAuthorityExpiryHeight) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "this token's mint authority expired at height " + std::to_string(entry.nMintAuthorityExpiryHeight));
        }
        if (entry.nCurrentSupply + amountToMint < entry.nCurrentSupply ||
            entry.nCurrentSupply + amountToMint > entry.nSupplyCap) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "amountToMint would exceed the token's supplyCap");
        }
    }

    CTxOut mintOut(0, GetScriptForDestination(mintDest));
    mintOut.tokenID = tokenID;
    mintOut.nTokenAmount = amountToMint;
    rawTx.vout.push_back(mintOut);

    rawTx.nTokenTxType = TOKEN_TX_MINT;
    rawTx.tokenMintPayload.tokenID = tokenID;
    rawTx.tokenMintPayload.nAmountToMint = amountToMint;

    return EncodeHexTx(CTransaction(rawTx));
},
    };
}

static RPCHelpMan createtokenmetadataupdatetx()
{
    return RPCHelpMan{"createtokenmetadataupdatetx",
                "\nBuilds an unsigned, unfunded raw transaction that updates a token's\n"
                "off-chain metadata pointer (metadataUri) and content hash (metadataHash).\n"
                "\"inputs\" must include at least one UTXO spendable by the token's issuer\n"
                "address, same authorization pattern as createtokenminttx. Once a token's\n"
                "metadata is set immutable (via setImmutable=true on any update), this RPC\n"
                "will be rejected at the consensus level for that token from then on.\n",
                {
                    {"inputs", RPCArg::Type::ARR, RPCArg::Optional::NO, "Inputs, at least one of which must be spendable by the token's issuer address.",
                        {
                            {"", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                                {
                                    {"txid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The transaction id"},
                                    {"vout", RPCArg::Type::NUM, RPCArg::Optional::NO, "The output number"},
                                },
                                },
                        },
                        },
                    {"token", RPCArg::Type::OBJ, RPCArg::Optional::NO, "Metadata update details",
                        {
                            {"tokenid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The token ID"},
                            {"metadataUri", RPCArg::Type::STR, RPCArg::Optional::NO, "Off-chain metadata pointer (IPFS/HTTP), max 256 chars"},
                            {"metadataHash", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "SHA256 hash of the off-chain metadata JSON content"},
                            {"setImmutable", RPCArg::Type::BOOL, /* default */ "false", "If true, permanently locks this token's metadata -- no further createtokenmetadataupdatetx will be accepted for it"},
                        },
                        },
                },
                RPCResult{
                    RPCResult::Type::STR_HEX, "", "the unsigned, unfunded raw transaction hex"
                },
                RPCExamples{
                    HelpExampleCli("createtokenmetadataupdatetx",
                        "\"[{\\\"txid\\\":\\\"issuerutxo\\\",\\\"vout\\\":0}]\" "
                        "\"{\\\"tokenid\\\":\\\"a1b2c3...\\\",\\\"metadataUri\\\":\\\"ipfs://Qm...\\\","
                        "\\\"metadataHash\\\":\\\"deadbeef...\\\"}\"")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    if (request.params[0].isNull() || request.params[0].get_array().size() == 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "inputs must be a non-empty array");
    }

    UniValue emptyOutputs(UniValue::VOBJ);
    CMutableTransaction rawTx = ConstructTransaction(request.params[0], emptyOutputs, NullUniValue, false);

    const UniValue& tokenArg = request.params[1];
    if (!tokenArg.isObject()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "token object is required");
    }

    uint256 tokenID(ParseHashV(tokenArg["tokenid"], "tokenid"));
    std::string metadataUri = tokenArg["metadataUri"].get_str();
    if (metadataUri.size() > MAX_METADATA_URI_LEN) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "metadataUri exceeds max length of " + std::to_string(MAX_METADATA_URI_LEN));
    }
    uint256 metadataHash(ParseHashV(tokenArg["metadataHash"], "metadataHash"));
    bool fSetImmutable = tokenArg["setImmutable"].isNull() ? false : tokenArg["setImmutable"].get_bool();

    {
        LOCK(cs_main);
        CTokenRegistryEntry entry;
        if (!::ChainstateActive().TokenDB().ReadTokenRegistry(tokenID, entry)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Unknown token ID");
        }
        if (entry.fMetadataImmutable) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "this token's metadata is permanently immutable");
        }
    }

    rawTx.nTokenTxType = TOKEN_TX_METADATA_UPDATE;
    rawTx.tokenMetadataUpdatePayload.tokenID = tokenID;
    rawTx.tokenMetadataUpdatePayload.metadataUri = metadataUri;
    rawTx.tokenMetadataUpdatePayload.metadataHash = metadataHash;
    rawTx.tokenMetadataUpdatePayload.fSetImmutable = fSetImmutable;

    // This tx type has no natural token-colored output (unlike mint/burn),
    // but fundrawtransaction requires at least one output to operate on --
    // add a zero-value OP_RETURN anchor, same pattern as the TX_ISSUE
    // anti-spam burn fee output elsewhere in this file.
    CTxOut anchorOut(0, CScript() << OP_RETURN);
    rawTx.vout.push_back(anchorOut);

    return EncodeHexTx(CTransaction(rawTx));
},
    };
}

static RPCHelpMan createtokentransfertx()
{
    return RPCHelpMan{"createtokentransfertx",
                "\nBuilds an unsigned, unfunded raw transaction that sends token supply from\n"
                "one or more token-colored UTXOs to one or more recipients -- a plain\n"
                "transfer, not a redemption. \"inputs\" must be token-colored UTXOs you\n"
                "control; \"outputs\" total per tokenID must not exceed the inputs' total\n"
                "for that tokenID (consensus requires an EXACT match, so include a change\n"
                "output back to yourself for any leftover). Fund any CIVIC fee with\n"
                "fundrawtransaction (these are normal spendable inputs, so it works\n"
                "normally), then sign and broadcast.\n",
                {
                    {"inputs", RPCArg::Type::ARR, RPCArg::Optional::NO, "The token-colored UTXOs to spend.",
                        {
                            {"", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                                {
                                    {"txid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The transaction id"},
                                    {"vout", RPCArg::Type::NUM, RPCArg::Optional::NO, "The output number"},
                                },
                                },
                        },
                        },
                    {"outputs", RPCArg::Type::ARR, RPCArg::Optional::NO, "The recipients. Include a change entry back to yourself for any leftover per tokenID.",
                        {
                            {"", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                                {
                                    {"address", RPCArg::Type::STR, RPCArg::Optional::NO, "Recipient address"},
                                    {"tokenid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The token ID being sent"},
                                    {"amount", RPCArg::Type::NUM, RPCArg::Optional::NO, "Amount to send, smallest unit"},
                                },
                                },
                        },
                        },
                },
                RPCResult{
                    RPCResult::Type::STR_HEX, "", "the unsigned, unfunded raw transaction hex"
                },
                RPCExamples{
                    HelpExampleCli("createtokentransfertx",
                        "\"[{\\\"txid\\\":\\\"myutxo\\\",\\\"vout\\\":1}]\" "
                        "\"[{\\\"address\\\":\\\"recipient\\\",\\\"tokenid\\\":\\\"a1b2c3...\\\",\\\"amount\\\":1000}]\"")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    if (request.params[0].isNull() || request.params[0].get_array().size() == 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "inputs must be a non-empty array");
    }
    if (request.params[1].isNull() || request.params[1].get_array().size() == 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "outputs must be a non-empty array");
    }

    UniValue emptyOutputs(UniValue::VOBJ);
    CMutableTransaction rawTx = ConstructTransaction(request.params[0], emptyOutputs, NullUniValue, false);

    const UniValue& outputsArg = request.params[1].get_array();
    std::map<uint256, uint64_t> outputTotals;
    for (unsigned int i = 0; i < outputsArg.size(); i++) {
        const UniValue& o = outputsArg[i];
        if (!o.isObject()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "each entry in outputs must be an object");
        }
        std::string addressStr = o["address"].get_str();
        CTxDestination dest = DecodeDestination(addressStr);
        if (!IsValidDestination(dest)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address in outputs");
        }
        uint256 tokenID(ParseHashV(o["tokenid"], "tokenid"));
        uint64_t amount = (uint64_t)o["amount"].get_int64();
        if (amount == 0) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "amount must be positive");
        }
        CTxOut txout(0, GetScriptForDestination(dest));
        txout.tokenID = tokenID;
        txout.nTokenAmount = amount;
        rawTx.vout.push_back(txout);
        outputTotals[tokenID] += amount;
    }

    // --- Fee-on-transfer: for every tokenID actually being moved by this
    //     tx's inputs, check if it's a fee token and enforce/auto-complete
    //     accordingly. Computed from total INPUT per tokenID (matching
    //     consensus exactly), not from the outputs the caller specified. ---
    {
        LOCK(cs_main);
        CTokenDB& tokenDB = ::ChainstateActive().TokenDB();
        int nextHeight = ::ChainActive().Height() + 1;
        std::map<uint256, uint64_t> inputTotals;
        for (const CTxIn& txin : rawTx.vin) {
            CTokenCoin tcoin;
            if (tokenDB.ReadTokenCoin(txin.prevout, tcoin)) {
                inputTotals[tcoin.tokenID] += tcoin.nTokenAmount;
            }
        }
        for (const auto& kv : inputTotals) {
            const uint256& tokenID = kv.first;
            uint64_t inAmt = kv.second;
            CTokenRegistryEntry entry;
            if (!tokenDB.ReadTokenRegistry(tokenID, entry) || !entry.HasTransferFee()) {
                continue;
            }
            uint64_t outAmt = outputTotals.count(tokenID) ? outputTotals[tokenID] : 0;
            uint16_t feeBps;
            if (entry.nFeeMode == TOKEN_FEE_MODE_RECIPIENT_CURVE) {
                feeBps = GetCurrentFeeBpsForCurve(entry.nFeeBpsStart, entry.nFeeBpsEnd,
                                                   entry.nFeeDecayDurationBlocks, entry.nIssueHeight, nextHeight);
            } else {
                feeBps = entry.nFeeBpsBurn;
            }
            unsigned __int128 feeCalc = (unsigned __int128)inAmt * (unsigned __int128)feeBps / (unsigned __int128)10000;
            uint64_t fee = (uint64_t)feeCalc;

            if (entry.nFeeMode == TOKEN_FEE_MODE_RECIPIENT_CURVE) {
                if (outAmt + fee != inAmt) {
                    throw JSONRPCError(RPC_INVALID_PARAMETER,
                        "outputs for tokenid " + tokenID.GetHex() + " total " + std::to_string(outAmt) +
                        ", but inputs total " + std::to_string(inAmt) + " and this token charges a " +
                        std::to_string(feeBps) + " bps transfer fee (" + std::to_string(fee) +
                        ") -- outputs must total exactly " + std::to_string(inAmt - fee) +
                        " to leave room for the mandatory fee output");
                }
                if (fee > 0) {
                    CScript feeScript;
                    feeScript << OP_0 << std::vector<unsigned char>(entry.feeRecipientHash160.begin(), entry.feeRecipientHash160.end());
                    CTxOut feeOut(0, feeScript);
                    feeOut.tokenID = tokenID;
                    feeOut.nTokenAmount = fee;
                    rawTx.vout.push_back(feeOut);
                }
            } else { // TOKEN_FEE_MODE_BURN_FLAT
                if (outAmt + fee != inAmt) {
                    throw JSONRPCError(RPC_INVALID_PARAMETER,
                        "outputs for tokenid " + tokenID.GetHex() + " total " + std::to_string(outAmt) +
                        ", but inputs total " + std::to_string(inAmt) + " and this token burns " +
                        std::to_string(feeBps) + " bps (" + std::to_string(fee) + ") on every transfer -- "
                        "outputs must total exactly " + std::to_string(inAmt - fee));
                }
                // No output for burned fee -- it simply reduces nCurrentSupply at validation.
            }
        }
    }

    return EncodeHexTx(CTransaction(rawTx));
},
    };
}

static RPCHelpMan createtokenvestingreleasetx()
{
    return RPCHelpMan{"createtokenvestingreleasetx",
                "\nBuilds an unsigned, unfunded raw transaction that releases the currently-\n"
                "vested portion of a vesting-locked token, per its schedule. \"inputs\" must\n"
                "include the vesting-lock UTXO (auto-detected from chain state) plus at\n"
                "least one UTXO spendable by the beneficiary address -- consensus verifies\n"
                "this via the input's signature plus a scriptPubKey match against the\n"
                "lock's beneficiary, so any input you don't control simply makes the tx\n"
                "unsignable/invalid. amountToRelease must not exceed what has vested by the\n"
                "confirming block's height, per the schedule set at issuance -- query\n"
                "gettokeninfo/listtokenunspent to check current status. Fund any remaining\n"
                "fee with fundrawtransaction (the non-lock inputs are normal spendable\n"
                "coins), then sign and broadcast -- this RPC never touches wallet or key\n"
                "material itself.\n",
                {
                    {"inputs", RPCArg::Type::ARR, RPCArg::Optional::NO, "The vesting-lock UTXO and at least one beneficiary-spendable UTXO.",
                        {
                            {"", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                                {
                                    {"txid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The transaction id"},
                                    {"vout", RPCArg::Type::NUM, RPCArg::Optional::NO, "The output number"},
                                },
                                },
                        },
                        },
                    {"token", RPCArg::Type::OBJ, RPCArg::Optional::NO, "Release details",
                        {
                            {"tokenid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The token ID"},
                            {"amountToRelease", RPCArg::Type::NUM, RPCArg::Optional::NO, "Amount to release now, smallest unit"},
                            {"beneficiaryAddress", RPCArg::Type::STR, RPCArg::Optional::NO, "Address to receive the released tokens"},
                            {"feeAmount", RPCArg::Type::AMOUNT, RPCArg::Optional::OMITTED, "Miner fee, required if a plain CIVIC funding UTXO is included in inputs (the vesting-lock input cannot be sized by the wallet, so fundrawtransaction cannot be used for this tx type -- fund manually)"},
                            {"changeAddress", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "Address for leftover CIVIC change from the funding input, required if funding exceeds feeAmount"},
                        },
                        },
                },
                RPCResult{
                    RPCResult::Type::STR_HEX, "", "the unsigned, unfunded raw transaction hex"
                },
                RPCExamples{
                    HelpExampleCli("createtokenvestingreleasetx", "\"[...]\" \"{...}\"")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    if (request.params[0].isNull() || request.params[0].get_array().size() == 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "inputs must be a non-empty array");
    }

    UniValue emptyOutputs(UniValue::VOBJ);
    CMutableTransaction rawTx = ConstructTransaction(request.params[0], emptyOutputs, NullUniValue, false);

    const UniValue& tokenArg = request.params[1];
    if (!tokenArg.isObject()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "token object is required");
    }

    uint256 tokenID(ParseHashV(tokenArg["tokenid"], "tokenid"));
    uint64_t amountToRelease = (uint64_t)tokenArg["amountToRelease"].get_int64();
    if (amountToRelease == 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "amountToRelease must be positive");
    }
    std::string beneficiaryAddressStr = tokenArg["beneficiaryAddress"].get_str();
    CTxDestination beneficiaryDest = DecodeDestination(beneficiaryAddressStr);
    if (!IsValidDestination(beneficiaryDest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid beneficiaryAddress");
    }
    CScript beneficiaryScript = GetScriptForDestination(beneficiaryDest);

    CTokenVestingLockData vd;
    uint64_t nCurrentLockedAmount = 0;
    bool foundLock = false;
    {
        LOCK(cs_main);
        for (const CTxIn& txin : rawTx.vin) {
            const Coin& coin = ::ChainstateActive().CoinsTip().AccessCoin(txin.prevout);
            if (coin.IsSpent()) continue;
            CTokenVestingLockData thisVd;
            if (IsTokenVestingLockScript(coin.out.scriptPubKey, &thisVd) && thisVd.tokenID == tokenID) {
                if (foundLock) {
                    throw JSONRPCError(RPC_INVALID_PARAMETER, "inputs contains more than one vesting-lock UTXO for this token");
                }
                foundLock = true;
                vd = thisVd;
                CTokenCoin tcoin;
                if (::ChainstateActive().TokenDB().ReadTokenCoin(txin.prevout, tcoin) && tcoin.tokenID == tokenID) {
                    nCurrentLockedAmount = tcoin.nTokenAmount;
                }
            }
        }
    }
    if (!foundLock) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "inputs must include the token's vesting-lock UTXO");
    }

    CAmount nFundingTotal = 0;
    {
        LOCK(cs_main);
        for (const CTxIn& txin : rawTx.vin) {
            const Coin& coin = ::ChainstateActive().CoinsTip().AccessCoin(txin.prevout);
            if (coin.IsSpent()) continue;
            CTokenVestingLockData thisVd;
            if (IsTokenVestingLockScript(coin.out.scriptPubKey, &thisVd) && thisVd.tokenID == tokenID) continue;
            nFundingTotal += coin.out.nValue;
        }
    }

    if (beneficiaryScript.size() != 22 || beneficiaryScript[0] != 0x00 || beneficiaryScript[1] != 0x14 ||
        uint160(std::vector<unsigned char>(beneficiaryScript.begin() + 2, beneficiaryScript.end())) != vd.beneficiaryHash160) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "beneficiaryAddress does not match this lock's beneficiary");
    }

    int nextHeight;
    {
        LOCK(cs_main);
        nextHeight = ::ChainActive().Height() + 1;
    }
    uint64_t allowedCumulative = GetVestedCumulativeAmount(vd, nextHeight);
    uint64_t alreadyReleased = vd.nOriginalTotalAmount - nCurrentLockedAmount;
    uint64_t availableNow = (allowedCumulative > alreadyReleased) ? (allowedCumulative - alreadyReleased) : 0;
    if (amountToRelease > availableNow) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "amountToRelease exceeds the currently-vested, unreleased amount (" + std::to_string(availableNow) + ")");
    }
    uint64_t nNewLockedAmount = nCurrentLockedAmount - amountToRelease;

    CTxOut releaseOut(0, beneficiaryScript);
    releaseOut.tokenID = tokenID;
    releaseOut.nTokenAmount = amountToRelease;
    rawTx.vout.push_back(releaseOut);

    if (nNewLockedAmount > 0) {
        CTxOut newLockOut(0, BuildTokenVestingLockScript(vd));
        newLockOut.tokenID = tokenID;
        newLockOut.nTokenAmount = nNewLockedAmount;
        rawTx.vout.push_back(newLockOut);
    }

    if (nFundingTotal > 0) {
        if (tokenArg["feeAmount"].isNull()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "feeAmount is required when a plain CIVIC funding input is included");
        }
        CAmount nFeeAmount = AmountFromValue(tokenArg["feeAmount"]);
        if (nFeeAmount <= 0 || nFeeAmount > nFundingTotal) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "feeAmount must be positive and not exceed the funding input total");
        }
        CAmount nChange = nFundingTotal - nFeeAmount;
        if (nChange > 0) {
            if (tokenArg["changeAddress"].isNull()) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "changeAddress is required: funding input exceeds feeAmount");
            }
            CTxDestination fundingChangeDest = DecodeDestination(tokenArg["changeAddress"].get_str());
            if (!IsValidDestination(fundingChangeDest)) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid changeAddress");
            }
            rawTx.vout.push_back(CTxOut(nChange, GetScriptForDestination(fundingChangeDest)));
        }
    }

    rawTx.nTokenTxType = TOKEN_TX_VESTING_RELEASE;
    rawTx.tokenVestingReleasePayload.tokenID = tokenID;
    rawTx.tokenVestingReleasePayload.nAmountToRelease = amountToRelease;

    return EncodeHexTx(CTransaction(rawTx));
},
    };
}

static RPCHelpMan createtokenburntx()
{
    return RPCHelpMan{"createtokenburntx",
                "\nBuilds an unsigned, unfunded raw transaction that permanently destroys\n"
                "owned tokens. Self-authorized: unlike createtokenminttx, no issuer or\n"
                "authority check is involved -- burning your own tokens only ever needs\n"
                "your own signature on the token-colored input(s) being spent, same as any\n"
                "plain transfer. If you burn less than the full input amount, include a\n"
                "changeAddress to receive the leftover (unburned) portion; otherwise the\n"
                "entire input is destroyed. Fund any CIVIC fee with fundrawtransaction\n"
                "(these are normal spendable inputs), then sign and broadcast.\n",
                {
                    {"inputs", RPCArg::Type::ARR, RPCArg::Optional::NO, "The token-colored UTXO(s) to burn from.",
                        {
                            {"", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                                {
                                    {"txid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The transaction id"},
                                    {"vout", RPCArg::Type::NUM, RPCArg::Optional::NO, "The output number"},
                                },
                                },
                        },
                        },
                    {"token", RPCArg::Type::OBJ, RPCArg::Optional::NO, "Burn details",
                        {
                            {"tokenid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The token ID"},
                            {"amountToBurn", RPCArg::Type::NUM, RPCArg::Optional::NO, "Amount to permanently destroy, smallest unit"},
                            {"changeAddress", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "Address for leftover (unburned) tokens, required if the inputs exceed amountToBurn"},
                        },
                        },
                },
                RPCResult{
                    RPCResult::Type::STR_HEX, "", "the unsigned, unfunded raw transaction hex"
                },
                RPCExamples{
                    HelpExampleCli("createtokenburntx",
                        "\"[{\\\"txid\\\":\\\"myutxo\\\",\\\"vout\\\":1}]\" "
                        "\"{\\\"tokenid\\\":\\\"a1b2c3...\\\",\\\"amountToBurn\\\":1000000}\"")
                },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    if (request.params[0].isNull() || request.params[0].get_array().size() == 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "inputs must be a non-empty array");
    }

    UniValue emptyOutputs(UniValue::VOBJ);
    CMutableTransaction rawTx = ConstructTransaction(request.params[0], emptyOutputs, NullUniValue, false);

    const UniValue& tokenArg = request.params[1];
    if (!tokenArg.isObject()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "token object is required");
    }

    uint256 tokenID(ParseHashV(tokenArg["tokenid"], "tokenid"));
    uint64_t amountToBurn = (uint64_t)tokenArg["amountToBurn"].get_int64();
    if (amountToBurn == 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "amountToBurn must be positive");
    }

    uint64_t nInputTotal = 0;
    {
        LOCK(cs_main);
        CTokenDB& tokenDB = ::ChainstateActive().TokenDB();
        for (const CTxIn& txin : rawTx.vin) {
            CTokenCoin tcoin;
            if (tokenDB.ReadTokenCoin(txin.prevout, tcoin) && tcoin.tokenID == tokenID) {
                nInputTotal += tcoin.nTokenAmount;
            }
        }
    }
    if (nInputTotal < amountToBurn) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "inputs total " + std::to_string(nInputTotal) + " is less than amountToBurn");
    }
    uint64_t nChange = nInputTotal - amountToBurn;

    if (nChange > 0) {
        if (tokenArg["changeAddress"].isNull()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "changeAddress is required: inputs exceed amountToBurn");
        }
        CTxDestination changeDest = DecodeDestination(tokenArg["changeAddress"].get_str());
        if (!IsValidDestination(changeDest)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid changeAddress");
        }
        CTxOut changeOut(0, GetScriptForDestination(changeDest));
        changeOut.tokenID = tokenID;
        changeOut.nTokenAmount = nChange;
        rawTx.vout.push_back(changeOut);
    }

    rawTx.nTokenTxType = TOKEN_TX_BURN;
    rawTx.tokenBurnPayload.tokenID = tokenID;
    rawTx.tokenBurnPayload.nAmountToBurn = amountToBurn;

    return EncodeHexTx(CTransaction(rawTx));
},
    };
}
void RegisterTokenRPCCommands(CRPCTable &t)
{
// clang-format off
static const CRPCCommand commands[] =
{ //  category    name                           actor (function)              argNames
  //  ----------- ------------------------------ ------------------------------ ----------
    { "token",    "gettokeninfo",                &gettokeninfo,                {"tokenid"} },
    { "token",    "listtokens",                  &listtokens,                  {} },
    { "token",    "gettokenbalance",             &gettokenbalance,             {"address", "tokenid"} },
    { "token",    "listtokenunspent",            &listtokenunspent,            {"tokenid", "address"} },
    { "token",    "createtokenissuetx",          &createtokenissuetx,          {"inputs", "token"} },
    { "token",    "createtokenconverttx",        &createtokenconverttx,        {"inputs", "token"} },
    { "token",    "createtokenminttx",           &createtokenminttx,           {"inputs", "token"} },
    { "token",    "createtokenmetadataupdatetx", &createtokenmetadataupdatetx, {"inputs", "token"} },
    { "token",    "createtokentransfertx",       &createtokentransfertx,       {"inputs", "outputs"} },
    { "token",    "createtokenvestingreleasetx", &createtokenvestingreleasetx, {"inputs", "token"} },
    { "token",    "createtokenburntx",           &createtokenburntx,           {"inputs", "token"} },
};
// clang-format on
    for (const auto& c : commands) {
        t.appendCommand(c.name, &c);
    }
}
