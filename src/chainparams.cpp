#include <chainparams.h>
#include <chainparamsseeds.h>
#include <consensus/merkle.h>
#include <tinyformat.h>
#include <util/system.h>
#include <util/strencodings.h>
#include <versionbitsinfo.h>
#include <arith_uint256.h>
#include <assert.h>
#include <pow.h>

static CBlock CreateGenesisBlock(const char* pszTimestamp, const CScript& genesisOutputScript, uint32_t nTime, uint32_t nNonce, uint32_t nBits, int32_t nVersion, const CAmount& genesisReward)
{
    CMutableTransaction txNew;
    txNew.nVersion = 1;
    txNew.vin.resize(1);
    txNew.vout.resize(1);
    txNew.vin[0].scriptSig = CScript() << 486604799 << CScriptNum(4) << std::vector<unsigned char>((const unsigned char*)pszTimestamp, (const unsigned char*)pszTimestamp + strlen(pszTimestamp));
    txNew.vout[0].nValue = genesisReward;
    txNew.vout[0].scriptPubKey = genesisOutputScript;

    CBlock genesis;
    genesis.nTime    = nTime;
    genesis.nBits    = nBits;
    genesis.nNonce   = nNonce;
    genesis.nVersion = nVersion;
    genesis.vtx.push_back(MakeTransactionRef(std::move(txNew)));
    genesis.hashPrevBlock.SetNull();
    genesis.hashMerkleRoot = BlockMerkleRoot(genesis);
    return genesis;
}

class CMainParams : public CChainParams {
public:
    CMainParams() {
        strNetworkID = CBaseChainParams::MAIN;
        consensus.signet_blocks = false;
        consensus.signet_challenge.clear();
        consensus.nSubsidyHalvingInterval = 2100000;
        
        consensus.BIP34Height = 1;
        consensus.BIP34Hash = uint256();
        consensus.BIP16Height = 1;
        consensus.BIP65Height = 1;
        consensus.BIP66Height = 1;
        consensus.CSVHeight = 1;
        consensus.SegwitHeight = 1;

        consensus.powLimit = uint256S("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nPowTargetTimespan = 2 * 60 * 60;
        consensus.nPowTargetSpacing = 1 * 60;
        consensus.fPowAllowMinDifficultyBlocks = false;
        consensus.fPowNoRetargeting = false;
        consensus.nRuleChangeActivationThreshold = 6048;
        consensus.nMinerConfirmationWindow = 8064;
        
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 1199145601;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = 1230767999;
        // BUG FIX (found during PoS testing, not in the original 10-item
        // report): DEPLOYMENT_TAPROOT and DEPLOYMENT_MWEB were declared in
        // consensus/params.h but never explicitly configured here, leaving
        // their .bit/.nStartTime/.nTimeout fields uninitialized. This
        // caused ComputeBlockVersion() to signal a garbage bit on every
        // regular block -- which happened to collide with
        // VERSIONBITS_POS_FLAG (bit 16), making every PoW block
        // misidentified as a PoS block. Explicitly disabling both closes
        // the collision and matches the Low 10 finding that MWEB has no
        // real deployment parameters set.
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].bit = 2;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nStartTime = Consensus::BIP9Deployment::NEVER_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_TAPROOT].nTimeout = Consensus::BIP9Deployment::NEVER_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_MWEB].bit = 3;
        consensus.vDeployments[Consensus::DEPLOYMENT_MWEB].nStartTime = Consensus::BIP9Deployment::NEVER_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_MWEB].nTimeout = Consensus::BIP9Deployment::NEVER_ACTIVE;

        nDefaultPort = 9333;
        pchMessageStart[0] = 0xc1;
        pchMessageStart[1] = 0x76;
        pchMessageStart[2] = 0x51;
        pchMessageStart[3] = 0xf3;
        nPruneAfterHeight = 100000;
        m_assumed_blockchain_size = 2;
        m_assumed_chain_state_size = 1;

        const char* pszTimestamp = "CivicNet 2026: Empowering Low-Spec CPUs with Cutting-Edge Hybrid Blockchain";
        const CScript genesisOutputScript = CScript() << ParseHex("040184710fa689ad5023690c80f3a49c8f13f8d45b8c857fbcbc8bc4a8e4d3eb4b10f4d4604fa08dce601aaf0f470216fe1b51850b4acf21b179c45070ac7b03a9") << OP_CHECKSIG;
        genesis = CreateGenesisBlock(pszTimestamp, genesisOutputScript, 1783443600, 23600788, 0x1d1ca213, 1, 77 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("0xd241c02c655f2c8192df11147f51f9d5027807ff0aea0255b3dc149b68bd4c13"));
        assert(genesis.hashMerkleRoot == uint256S("0x42caec8d3d049fe36792bc316c82cf364a46e4467226c5abef280b8faef66ffd"));

        vSeeds.clear();
        vFixedSeeds.clear();
        vSeeds.push_back("103.180.165.99");

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1, 28);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1, 5);
        base58Prefixes[SCRIPT_ADDRESS2] = std::vector<unsigned char>(1, 50);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1, 176);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x88, 0xB2, 0x1E};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x88, 0xAD, 0xE4};

        bech32_hrp = "civc";
        // vFixedSeeds intentionally left empty - seedless during MWEB cleanup

        fDefaultConsistencyChecks = false;
        fRequireStandard = true;
        m_is_test_chain = false;
        m_is_mockable_chain = false;

        checkpointData = {{{0, consensus.hashGenesisBlock}}};
        chainTxData = ChainTxData{1783443600, 0, 0};
    }
};

class CTestNetParams : public CChainParams {
public:
    CTestNetParams() {
        strNetworkID = CBaseChainParams::TESTNET;
        consensus.nSubsidyHalvingInterval = 1050000;
        consensus.powLimit = uint256S("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nPowTargetTimespan = 3.5 * 24 * 60 * 60;
        consensus.nPowTargetSpacing = 1 * 60;
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.fPowNoRetargeting = false;
        nDefaultPort = 19333;
        pchMessageStart[0] = 0xd4;
        pchMessageStart[1] = 0x9a;
        pchMessageStart[2] = 0x2b;
        pchMessageStart[3] = 0x87;
        vSeeds.clear();
        vFixedSeeds.clear();

        const char* pszTimestamp = "CivicNet 2026: Empowering Low-Spec CPUs with Cutting-Edge Hybrid Blockchain";
        const CScript genesisOutputScript = CScript() << ParseHex("040184710fa689ad5023690c80f3a49c8f13f8d45b8c857fbcbc8bc4a8e4d3eb4b10f4d4604fa08dce601aaf0f470216fe1b51850b4acf21b179c45070ac7b03a9") << OP_CHECKSIG;
        genesis = CreateGenesisBlock(pszTimestamp, genesisOutputScript, 1783443600, 78042, 0x1e0ffff0, 1, 149 * COIN);

        consensus.hashGenesisBlock = genesis.GetHash();

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1, 111);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1, 196);
        base58Prefixes[SCRIPT_ADDRESS2] = std::vector<unsigned char>(1, 58);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1, 239);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};
        bech32_hrp = "tcivc";
        fDefaultConsistencyChecks = false;
        fRequireStandard = false;
        m_is_test_chain = true;
        m_is_mockable_chain = false;
        checkpointData = {};
        chainTxData = ChainTxData{0, 0, 0};
    }
};

class CSignetParams : public CChainParams {
public:
    CSignetParams() {
        strNetworkID = CBaseChainParams::SIGNET;
        consensus.nSubsidyHalvingInterval = 1050000;
        consensus.powLimit = uint256S("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nPowTargetTimespan = 3.5 * 24 * 60 * 60;
        consensus.nPowTargetSpacing = 1 * 60;
        consensus.fPowAllowMinDifficultyBlocks = false;
        consensus.fPowNoRetargeting = false;
        nDefaultPort = 39333;
        pchMessageStart[0] = 0xa2;
        pchMessageStart[1] = 0x3e;
        pchMessageStart[2] = 0x8f;
        pchMessageStart[3] = 0x64;
        vSeeds.clear();
        vFixedSeeds.clear();
    }
};

class CRegTestParams : public CChainParams {
public:
    CRegTestParams() {
        strNetworkID = CBaseChainParams::REGTEST;
        bech32_hrp = "rcivc";
        consensus.nSubsidyHalvingInterval = 150;
        consensus.powLimit = uint256S("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nPowTargetTimespan = 3.5 * 24 * 60 * 60;
        consensus.nPowTargetSpacing = 1 * 60;
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.fPowNoRetargeting = true;
        nDefaultPort = 19444;
        vSeeds.clear();
        vFixedSeeds.clear();
        const char* pszTimestamp = "CivicNet 2026: Empowering Low-Spec CPUs with Cutting-Edge Hybrid Blockchain";
        consensus.powLimit = uint256S("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        const CScript genesisOutputScript = CScript() << ParseHex("040184710fa689ad5023690c80f3a49c8f13f8d45b8c857fbcbc8bc4a8e4d3eb4b10f4d4604fa08dce601aaf0f470216fe1b51850b4acf21b179c45070ac7b03a9") << OP_CHECKSIG;
        genesis = CreateGenesisBlock(pszTimestamp, genesisOutputScript, 1783443600, 0, 0x1f7fffff, 1, 149 * COIN);
// 
//         // TEMPORARY: search for a valid nonce under the new civiclight_hash algorithm
//         if (true) {
//             fprintf(stderr, "Searching for valid regtest genesis nonce (using built-in CheckProofOfWork)...\n"); fflush(stderr);
//             for (uint32_t nonce = 0; nonce < 0xFFFFFFFF; nonce++) {
//                 genesis.nNonce = nonce;
//                 if (nonce < 5) {
//                     bool result = CheckProofOfWork(genesis.GetPoWHash(), genesis.nBits, consensus);
//                     arith_uint256 t; t.SetCompact(genesis.nBits);
//                     fprintf(stderr, "  nonce=%u hash=%s target=%s result=%d\n", nonce, genesis.GetPoWHash().ToString().c_str(), t.GetHex().c_str(), (int)result);
//                     fflush(stderr);
//                 }
//                 if (CheckProofOfWork(genesis.GetPoWHash(), genesis.nBits, consensus)) {
//                     fprintf(stderr, "FOUND regtest nonce: %u\n", nonce);
//                     fprintf(stderr, "Genesis hash: %s\n", genesis.GetHash().ToString().c_str());
//                     fflush(stderr);
//                     break;
//                 }
//                 if (nonce % 1000 == 0) { fprintf(stderr, "  tried %u...\n", nonce); fflush(stderr); }
//             }
//         }

        consensus.hashGenesisBlock = genesis.GetHash();

        fDefaultConsistencyChecks = true;
        fRequireStandard = true;
        m_is_test_chain = false;
        m_is_mockable_chain = true;
        checkpointData = {};
        chainTxData = ChainTxData{0, 0, 0};
    }
};

static std::unique_ptr<const CChainParams> globalChainParams;

const CChainParams &Params() {
    assert(globalChainParams);
    return *globalChainParams;
}

std::unique_ptr<const CChainParams> CreateChainParams(const ArgsManager& args, const std::string& chain)
{
    if (chain == CBaseChainParams::MAIN)
        return std::unique_ptr<CChainParams>(new CMainParams());
    else if (chain == CBaseChainParams::TESTNET)
        return std::unique_ptr<CChainParams>(new CTestNetParams());
    else if (chain == CBaseChainParams::SIGNET)
        return std::unique_ptr<CChainParams>(new CSignetParams());
    else if (chain == CBaseChainParams::REGTEST)
        return std::unique_ptr<CChainParams>(new CRegTestParams());
    throw std::runtime_error(strprintf("%s: Unknown chain %s.", __func__, chain));
}

void SelectParams(const std::string& network)
{
    SelectBaseParams(network);
    globalChainParams = CreateChainParams(gArgs, network);
}
