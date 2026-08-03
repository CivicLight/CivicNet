#ifndef BITCOIN_CHAINPARAMSSEEDS_H
#define BITCOIN_CHAINPARAMSSEEDS_H
/**
 * List of fixed seed nodes for the CivicNet network
 *
 * SECURITY FIX (Low 10): this file previously contained the upstream
 * Litecoin project's hardcoded seed list byte-for-byte -- 109 IP addresses
 * belonging to Litecoin nodes, not CivicNet nodes. Left empty here rather
 * than populated with placeholder/fake data. Real peer discovery for this
 * network relies on the DNS seed (dnsseed.civiclight.xyz) and the single
 * hardcoded seed node in chainparams.cpp -- this file can be regenerated
 * properly via contrib/seeds/generate-seeds.py once a real, multi-operator
 * seed list exists.
 */
static const uint8_t chainparams_seed_main[] = {};
static const uint8_t chainparams_seed_test[] = {};
#endif // BITCOIN_CHAINPARAMSSEEDS_H
