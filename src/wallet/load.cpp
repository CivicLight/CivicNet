// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2020 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <wallet/load.h>
#include <pos_kernel.h>
#include <chain.h>
#include <chainparams.h>
#include <consensus/merkle.h>
#include <validation.h>
#include <timedata.h>

#include <fs.h>
#include <interfaces/chain.h>
#include <scheduler.h>
#include <util/string.h>
#include <util/system.h>
#include <util/translation.h>
#include <wallet/wallet.h>
#include <wallet/walletdb.h>

#include <univalue.h>

bool VerifyWallets(interfaces::Chain& chain)
{
    if (gArgs.IsArgSet("-walletdir")) {
        fs::path wallet_dir = gArgs.GetArg("-walletdir", "");
        boost::system::error_code error;
        // The canonical path cleans the path, preventing >1 Berkeley environment instances for the same directory
        fs::path canonical_wallet_dir = fs::canonical(wallet_dir, error).remove_trailing_separator();
        if (error || !fs::exists(wallet_dir)) {
            chain.initError(strprintf(_("Specified -walletdir \"%s\" does not exist"), wallet_dir.string()));
            return false;
        } else if (!fs::is_directory(wallet_dir)) {
            chain.initError(strprintf(_("Specified -walletdir \"%s\" is not a directory"), wallet_dir.string()));
            return false;
        // The canonical path transforms relative paths into absolute ones, so we check the non-canonical version
        } else if (!wallet_dir.is_absolute()) {
            chain.initError(strprintf(_("Specified -walletdir \"%s\" is a relative path"), wallet_dir.string()));
            return false;
        }
        gArgs.ForceSetArg("-walletdir", canonical_wallet_dir.string());
    }

    LogPrintf("Using wallet directory %s\n", GetWalletDir().string());

    chain.initMessage(_("Verifying wallet(s)...").translated);

    // For backwards compatibility if an unnamed top level wallet exists in the
    // wallets directory, include it in the default list of wallets to load.
    if (!gArgs.IsArgSet("wallet")) {
        DatabaseOptions options;
        DatabaseStatus status;
        bilingual_str error_string;
        options.require_existing = true;
        options.verify = false;
        if (MakeWalletDatabase("", options, status, error_string)) {
            gArgs.LockSettings([&](util::Settings& settings) {
                util::SettingsValue wallets(util::SettingsValue::VARR);
                wallets.push_back(""); // Default wallet name is ""
                settings.rw_settings["wallet"] = wallets;
            });
        }
    }

    // Keep track of each wallet absolute path to detect duplicates.
    std::set<fs::path> wallet_paths;

    for (const auto& wallet_file : gArgs.GetArgs("-wallet")) {
        const fs::path path = fs::absolute(wallet_file, GetWalletDir());

        if (!wallet_paths.insert(path).second) {
            chain.initWarning(strprintf(_("Ignoring duplicate -wallet %s."), wallet_file));
            continue;
        }

        DatabaseOptions options;
        DatabaseStatus status;
        options.require_existing = true;
        options.verify = true;
        bilingual_str error_string;
        if (!MakeWalletDatabase(wallet_file, options, status, error_string)) {
            if (status == DatabaseStatus::FAILED_NOT_FOUND) {
                chain.initWarning(Untranslated(strprintf("Skipping -wallet path that doesn't exist. %s\n", error_string.original)));
            } else {
                chain.initError(error_string);
                return false;
            }
        }
    }

    return true;
}

bool LoadWallets(interfaces::Chain& chain)
{
    try {
        std::set<fs::path> wallet_paths;
        for (const std::string& name : gArgs.GetArgs("-wallet")) {
            if (!wallet_paths.insert(name).second) {
                continue;
            }
            DatabaseOptions options;
            DatabaseStatus status;
            options.require_existing = true;
            options.verify = false; // No need to verify, assuming verified earlier in VerifyWallets()
            bilingual_str error;
            std::vector<bilingual_str> warnings;
            std::unique_ptr<WalletDatabase> database = MakeWalletDatabase(name, options, status, error);
            if (!database && status == DatabaseStatus::FAILED_NOT_FOUND) {
                continue;
            }
            std::shared_ptr<CWallet> pwallet = database ? CWallet::Create(chain, name, std::move(database), options.create_flags, error, warnings) : nullptr;
            if (!warnings.empty()) chain.initWarning(Join(warnings, Untranslated("\n")));
            if (!pwallet) {
                chain.initError(error);
                return false;
            }
            AddWallet(pwallet);
        }
        return true;
    } catch (const std::runtime_error& e) {
        chain.initError(Untranslated(e.what()));
        return false;
    }
}

static void TryStakeAllWallets()
{
    // PoS: skip staking entirely during initial block download or a manual
    // block-generation pass -- submitting/writing blocks while the chain
    // itself is still catching up (or being manually extended) is what
    // caused the historical block-file corruption bugs on this project.
    if (::ChainstateActive().IsInitialBlockDownload()) return;
    // PoS: don't bother attempting kernel-finding/block submission before
    // the network-wide activation time -- IsProofOfStake() already rejects
    // such blocks at the consensus layer, so this is purely to avoid
    // wasted CPU and log spam before activation.
    if ((uint32_t)GetAdjustedTime() < CBlockHeader::POS_ACTIVATION_TIME) return;
    uint256 tipStakeModifier;
    uint32_t tipStakeTarget;
    uint32_t tipTime;
    uint256 tipHash;
    int tipHeight;
    uint32_t tipMedianTimePast;
    // CONSISTENCY FIX support: captured in the outer scope so it survives
    // past the cs_main lock block, for use with ComputeExpectedStakeTarget
    // further down when constructing the candidate block.
    CBlockIndex* capturedTip = nullptr;
    {
        LOCK(cs_main);
        CBlockIndex* tip = ::ChainActive().Tip();
        if (tip == nullptr) return;
        capturedTip = tip;
        tipStakeModifier = tip->nStakeModifier;
        tipStakeTarget = tip->nStakeTarget;
        tipTime = (uint32_t)tip->nTime;
        tipHash = tip->GetBlockHash();
        tipHeight = tip->nHeight;
        tipMedianTimePast = (uint32_t)tip->GetMedianTimePast();
    }

    uint32_t nowTime = std::max((uint32_t)GetAdjustedTime(), (uint32_t)tipMedianTimePast + 1);
    uint32_t candidateTime = NextValidStakeTimestamp(nowTime);

    arith_uint256 target;
    target.SetCompact(tipStakeTarget);
    if (target == 0) return; // PoS not yet active on this chain

    for (const std::shared_ptr<CWallet>& pwallet : GetWallets()) {
        if (pwallet->IsLocked()) continue;

        LogPrintf("PoS: staking check tick for wallet %s\n", pwallet->GetName());

        std::vector<COutputCoin> vCoins;
            {
        LOCK(pwallet->cs_wallet);
        pwallet->AvailableCoins(vCoins);
            }

        // PoS: only attempt ONE kernel/submission per wallet per tick.
        // Without this, a wallet with many eligible UTXOs (e.g. lots of
        // small past staking rewards) would find a valid kernel on EVERY
        // one of them in the same tick and submit many blocks back-to-back
        // -- only the first can ever connect, the rest are correctly
        // rejected as bad-cs-clustering but still cost a full block
        // construction/signing/ProcessNewBlock cycle each, in a tight loop
        // with no delay. On slower machines this tight loop is a likely
        // cause of the wallet appearing to freeze during/after sync.
        bool submittedThisTick = false;
        for (const auto& coin : vCoins) {
            if (submittedThisTick) break;
            CAmount balance = coin.GetInputCoin().GetAmount();
            bool found = CheckStakeKernel(
                tipStakeModifier,
                tipTime,
                (uint32_t)coin.GetDepth(), // block-depth substitute for tx offset, consistent with ConnectBlock's approach
                (uint32_t)coin.GetInputCoin().GetOutpoint().n,
                candidateTime,
                balance,
                target
            );
            if (found) {
                LogPrintf("PoS: valid stake kernel found for wallet %s, amount=%d, nTimeTx=%u\n", pwallet->GetName(), balance, candidateTime);

                CMutableTransaction coinstake;
                // SECURITY FIX (Medium 7) support: captured in the outer
                // scope so it survives past the cs_wallet lock block below,
                // for use when signing the finished block further down.
                CTxDestination stakerDest;
                {
                    LOCK(pwallet->cs_wallet);
                CTxDestination dest;
                if (!coin.GetDestination(dest)) {
                    LogPrintf("PoS: could not extract destination for coinstake output\n");
                    continue;
                }
                stakerDest = dest;
                CScript stakerScript = GetScriptForDestination(dest);

                coinstake.vin.emplace_back(coin.GetInputCoin().GetOutpoint());
                coinstake.vout.emplace_back(0, CScript());
                coinstake.vout.emplace_back(balance + GetBlockSubsidy(tipHeight + 1, ::Params().GetConsensus()), stakerScript);

                if (!pwallet->SignTransaction(coinstake)) {
                    LogPrintf("PoS: failed to sign coinstake transaction\n");
                    continue;
                }
                }

                CBlock block;
                block.nVersion = 0x20000000 | CBlockHeader::VERSIONBITS_POS_FLAG;
                block.hashPrevBlock = tipHash;
                block.nTime = candidateTime;
                // CONSISTENCY FIX: use the same computation the validator
                // uses (ComputeExpectedStakeTarget), not a raw copy of the
                // chain's current stake target. These only coincidentally
                // matched so far because no real PoS block has occurred yet
                // (nLastPoSHeight == -1, so the validator's retarget branch
                // never triggers) -- once staking has been live for a
                // while, a raw copy here would start disagreeing with what
                // the validator expects.
                block.nBits = ComputeExpectedStakeTarget(capturedTip, block);

                CMutableTransaction coinbase;
                coinbase.vin.emplace_back(COutPoint(), CScript() << (tipHeight + 1) << OP_0);
                coinbase.vout.emplace_back(0, CScript());
                block.vtx.push_back(MakeTransactionRef(coinbase));
                block.vtx.push_back(MakeTransactionRef(coinstake));
                GenerateCoinbaseCommitment(block, ::ChainActive().Tip(), ::Params().GetConsensus());

                bool mutated;
                block.hashMerkleRoot = BlockMerkleRoot(block, &mutated);

                // SECURITY FIX (Medium 7): sign the block hash with the
                // staker's own key -- the same key controlling the
                // coinstake's kernel input. Without this, the coinstake tx
                // alone doesn't bind to any specific block, and anyone
                // observing a valid PoS block could rebuild a different
                // block around the same coinstake transaction.
                {
                    CKeyID keyID;
                if (const PKHash* pkhash = boost::get<PKHash>(&stakerDest)) {
                    keyID = ToKeyID(*pkhash);
                } else if (const WitnessV0KeyHash* witnessID = boost::get<WitnessV0KeyHash>(&stakerDest)) {
                    keyID = CKeyID();
                    std::copy(witnessID->begin(), witnessID->end(), keyID.begin());
                } else {
                    LogPrintf("PoS: unsupported address type for block signature\n");
                    continue;
                }
                    CKey stakerKey;
                    if (!pwallet->GetLegacyScriptPubKeyMan()->GetKey(keyID, stakerKey)) {
                        LogPrintf("PoS: failed to get staker key for block signature\n");
                        continue;
                    }
                    uint256 blockHashToSign = block.GetHash();
                    if (!stakerKey.SignCompact(blockHashToSign, block.vchBlockSig)) {
                        LogPrintf("PoS: failed to sign block\n");
                        continue;
                    }
                }
                std::shared_ptr<const CBlock> shared_pblock = std::make_shared<const CBlock>(block);
                bool submitOk;
                {
                    LOCK(g_process_new_block_mutex);
                    submitOk = g_chainman.ProcessNewBlock(Params(), shared_pblock, true, nullptr);
                }
                submittedThisTick = true;
                if (submitOk) {
                    LogPrintf("PoS: block submitted successfully, hash=%s\n", block.GetHash().ToString());
                } else {
                    LogPrintf("PoS: block submission FAILED\n");
                }
            }
        }
    }
}

void StartWallets(CScheduler& scheduler, const ArgsManager& args)
{
    for (const std::shared_ptr<CWallet>& pwallet : GetWallets()) {
        pwallet->postInitProcess();
    }

    // Schedule periodic wallet flushes and tx rebroadcasts
    if (args.GetBoolArg("-flushwallet", DEFAULT_FLUSHWALLET)) {
        scheduler.scheduleEvery(MaybeCompactWalletDB, std::chrono::milliseconds{500});
    }
    scheduler.scheduleEvery(MaybeResendWalletTxs, std::chrono::milliseconds{1000});

    // PoS: check for a valid stake roughly every STAKE_TIMESTAMP_MASK
    // seconds, matching the granularity of nTimeTx candidates.
    scheduler.scheduleEvery(TryStakeAllWallets, std::chrono::milliseconds{8000});
}

void FlushWallets()
{
    for (const std::shared_ptr<CWallet>& pwallet : GetWallets()) {
        pwallet->Flush();
    }
}

void StopWallets()
{
    for (const std::shared_ptr<CWallet>& pwallet : GetWallets()) {
        pwallet->Close();
    }
}

void UnloadWallets()
{
    auto wallets = GetWallets();
    while (!wallets.empty()) {
        auto wallet = wallets.back();
        wallets.pop_back();
        std::vector<bilingual_str> warnings;
        RemoveWallet(wallet, nullopt, warnings);
        UnloadWallet(std::move(wallet));
    }
}
