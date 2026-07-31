#include <pos_kernel.h>
#include <iostream>
#include <cassert>

int main() {
    // Test 1: ComputeStakeModifier determinism
    uint256 prevMod = uint256S("1111111111111111111111111111111111111111111111111111111111111111");
    uint256 refHash = uint256S("2222222222222222222222222222222222222222222222222222222222222222");
    uint32_t refHeight = 120;

    uint256 mod1 = ComputeStakeModifier(prevMod, refHash, refHeight);
    uint256 mod2 = ComputeStakeModifier(prevMod, refHash, refHeight);

    assert(mod1 == mod2);
    std::cout << "Test 1 PASSED: ComputeStakeModifier is deterministic" << std::endl;
    std::cout << "  Modifier: " << mod1.ToString() << std::endl;

    // Test 2: Different inputs give different modifiers
    uint256 mod3 = ComputeStakeModifier(prevMod, refHash, refHeight + 1);
    assert(mod1 != mod3);
    std::cout << "Test 2 PASSED: different height gives different modifier" << std::endl;

    // Test 3: CheckStakeKernel with high target (should mostly pass) vs low target
    arith_uint256 highTarget = arith_uint256(1) << 250; // very easy
    arith_uint256 lowTarget = arith_uint256(1);          // very hard

    uint256 prevHash = uint256S("3333333333333333333333333333333333333333333333333333333333333333");
    bool easyResult = CheckStakeKernel(mod1, 1000, 0, 0, 2000, 1000, highTarget);
    bool hardResult = CheckStakeKernel(mod1, 1000, 0, 0, 2000, 1000, lowTarget);

    std::cout << "Test 3: easy target result=" << easyResult << ", hard target result=" << hardResult << std::endl;
    assert(easyResult == true);
    assert(hardResult == false);
    std::cout << "Test 3 PASSED: target scaling works as expected" << std::endl;

    // Test 4: higher balance should be easier to satisfy (sqrt weighting)
    int64_t lowBalance = 100;
    int64_t highBalance = 1000000;
    arith_uint256 midTarget = arith_uint256(1) << 200;

    bool lowBalResult = CheckStakeKernel(mod1, 1000, 0, 0, 2000, lowBalance, midTarget);
    bool highBalResult = CheckStakeKernel(mod1, 1000, 0, 0, 2000, highBalance, midTarget);
    std::cout << "Test 4: low balance result=" << lowBalResult << ", high balance result=" << highBalResult << std::endl;

    // Test 5: nTimeTx validation
    uint32_t refTime = 1000000;
    assert(IsValidStakeTimestamp(1000008, refTime) == true);   // aligned, after
    assert(IsValidStakeTimestamp(1000005, refTime) == false);  // not aligned
    assert(IsValidStakeTimestamp(999992, refTime) == false);   // before refTime
    std::cout << "Test 5 PASSED: IsValidStakeTimestamp works correctly" << std::endl;

    // Test 6: NextValidStakeTimestamp always returns a valid one
    uint32_t next = NextValidStakeTimestamp(refTime);
    assert(IsValidStakeTimestamp(next, refTime) == true);
    std::cout << "Test 6 PASSED: NextValidStakeTimestamp=" << next << " is valid" << std::endl;

    // Test 7: anti-clustering rule (N=2)
    assert(CanBeProofOfStake(100, -1) == true);   // no prior PoS, allowed
    assert(CanBeProofOfStake(100, 99) == false);  // gap=1, too close
    assert(CanBeProofOfStake(100, 98) == false);  // gap=2, still too close (must be > N)
    assert(CanBeProofOfStake(100, 97) == true);   // gap=3, allowed
    std::cout << "Test 7 PASSED: CanBeProofOfStake anti-clustering works correctly" << std::endl;

    // Test 8: stake modifier boundary detection (H=120)
    assert(IsStakeModifierBoundary(120) == true);
    assert(IsStakeModifierBoundary(240) == true);
    assert(IsStakeModifierBoundary(121) == false);
    assert(IsStakeModifierBoundary(0) == false);
    std::cout << "Test 8 PASSED: IsStakeModifierBoundary works correctly" << std::endl;

    // Test 9: reference height calculation (k=30)
    assert(GetStakeModifierReferenceHeight(120) == 90);
    assert(GetStakeModifierReferenceHeight(121) == -1);  // not a boundary
    assert(GetStakeModifierReferenceHeight(20) == -1);   // boundary check fails first (20 % 120 != 0)
    std::cout << "Test 9 PASSED: GetStakeModifierReferenceHeight works correctly" << std::endl;

    // Test 10: dynamic PoW/PoS ratio adjustment
    int midRatio = 1000; // 10%, safely away from floor/ceiling for this test
    int r1 = AdjustPoSRatio(midRatio, 60, 50);  // over-deliver -> increase
    assert(r1 == midRatio + STAKE_RATIO_STEP_BPS);

    int r2 = AdjustPoSRatio(midRatio, 40, 50);  // under-deliver -> decrease
    assert(r2 == midRatio - STAKE_RATIO_STEP_BPS);

    int r3 = AdjustPoSRatio(STAKE_RATIO_FLOOR_BPS, 10, 50);  // already at floor, can't go lower
    assert(r3 == STAKE_RATIO_FLOOR_BPS);

    int r4 = AdjustPoSRatio(STAKE_RATIO_CEILING_BPS, 100, 50); // already at ceiling, can't go higher
    assert(r4 == STAKE_RATIO_CEILING_BPS);

    std::cout << "Test 10 PASSED: AdjustPoSRatio bounded correctly (floor/ceiling/step)" << std::endl;

    // Test 11: genesis stake modifier bootstrap
    uint256 genesisHash = uint256S("4444444444444444444444444444444444444444444444444444444444444444");
    uint32_t genesisTime = 1783443600;

    uint256 initMod1 = GetInitialStakeModifier(genesisHash, genesisTime);
    uint256 initMod2 = GetInitialStakeModifier(genesisHash, genesisTime);
    assert(initMod1 == initMod2);

    // Different genesis (e.g. testnet vs mainnet) must give a different modifier
    uint256 differentGenesis = uint256S("5555555555555555555555555555555555555555555555555555555555555555");
    uint256 initMod3 = GetInitialStakeModifier(differentGenesis, genesisTime);
    assert(initMod1 != initMod3);

    std::cout << "Test 11 PASSED: GetInitialStakeModifier is deterministic and genesis-specific" << std::endl;
    std::cout << "  Initial modifier: " << initMod1.ToString() << std::endl;

    // Test 12: PoS target adjustment (+/-8% bound, mirrors PoW retarget)
    arith_uint256 maxT = arith_uint256(1) << 250;
    arith_uint256 curTarget = arith_uint256(1) << 200;

    // Blocks coming too fast (actual < target) -> target should shrink (harder)
    arith_uint256 fastResult = GetNextStakeTarget(curTarget, 30, 60, maxT);
    assert(fastResult < curTarget);

    // Blocks coming too slow (actual > target) -> target should grow (easier)
    arith_uint256 slowResult = GetNextStakeTarget(curTarget, 120, 60, maxT);
    assert(slowResult > curTarget);

    // Result must never exceed maxTarget
    arith_uint256 nearMaxTarget = maxT;
    arith_uint256 cappedResult = GetNextStakeTarget(nearMaxTarget, 200, 60, maxT);
    assert(cappedResult <= maxT);

    std::cout << "Test 12 PASSED: GetNextStakeTarget adjusts correctly and respects max cap" << std::endl;

    std::cout << "\nAll tests completed." << std::endl;
    return 0;
}
