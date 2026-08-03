# CivicNet Changelog

This file is an append-only record of project milestones, protocol changes, and public commitments. Entries are added via git commit, not edited in place — the git history itself preserves the original text and timestamp of every entry.

---

## 2026-08-01 — Hybrid PoW+PoS Consensus Activated (v3.0.0)

- CivicNet Core v3.0.0 released, introducing Hybrid Proof-of-Work + Proof-of-Stake consensus — the design finalized in the whitepaper (see 2026-07-23 entry) is now implemented and deployed.
- Consensus is now shared between two block-production mechanisms on the same chain: CPU mining (PoW, unchanged) and staking (PoS, new). No minimum balance, no lock-up period, and no masternode tier required to stake — staking probability scales with the square root of a wallet's balance to prevent whale dominance, and an anti-clustering rule prevents consecutive PoS blocks from crowding out mining.
- Staking is gated by a hard-coded activation timestamp, **2026-08-04 09:00:00 UTC** — deployed ahead of this date with zero risk of premature activation, since PoS block validation is disabled at the consensus level below the threshold regardless of what a block claims.
- **Incident & fix, same day:** the first deployment attempt to the live mainnet node failed on startup (`LoadBlockIndexGuts: failed to read value`) due to new persistent fields added to the on-disk block index format, which the existing chain's already-written index entries did not contain. Rolled back immediately to the pre-PoS binary with zero downtime beyond the deployment window itself. Root cause identified as a block-index format compatibility issue (not a logic bug); fixed by starting the new binary with `-reindex` (a one-time local index rebuild from existing block data, no resync required). Verified first on an isolated copy of the datadir, then successfully redeployed to production the same day.
- Desktop wallet updated with a staking-only unlock mode (enables staking without exposing spending capability) and a live staking status indicator.
- Source code and downloads: https://github.com/CivicLight/CivicNet/releases/tag/v3.0.0

## 2026-07-23 — civiclight v2 Hard Fork Activated

- civiclight v2 (yespower-based, memory-hard) activated at **block 4794**, per the pre-announced timestamp threshold (1784797200 UTC).
- This closes the ASIC-resistance gap openly acknowledged in civiclight v1 (see 2026-07-19 entry below).
- **Incident & fix, same day:** difficulty was inherited unchanged from the last v1 block immediately after the fork, and the standard ±8%-per-block retarget cap was too slow to adjust to yespower's real hashrate — block 4794 was delayed several hours as a result. Root cause identified, a one-time difficulty reset was patched into `GetNextWorkRequired()` for the exact fork-transition block, tested on testnet, and deployed to mainnet the same day.
- Real network hashrate post-fork: ~14.7 kH/s (down from ~7 GH/s pre-fork), confirming most pre-fork hashrate was not coming from CPU hardware.
- Whitepaper published, covering the current architecture and a finalized (not yet implemented) design for a future Hybrid PoW+PoS model.

## 2026-07-19 — Domain, DNS Seed, and Public Acknowledgment of civiclight v1 Limitations

- Official domain civiclight.xyz went live, with pool/explorer/web wallet on proper HTTPS subdomains.
- A working DNS seed node (dnsseed.civiclight.xyz) went live, enabling automatic peer discovery.
- Publicly acknowledged (in response to community feedback) that civiclight v1 (SHA256 → XOR → SHA256) was CPU-friendly in practice but not cryptographically ASIC-resistant, and that a memory-hard v2 upgrade was on the roadmap.

## 2026-07-17 — Windows GUI Wallet Released (v1.0.1)

- First Windows GUI wallet release, including a pchMessageStart fix, auto-connect to the official node, and ticker fixes.

## 2026-07-16 — Web Wallet Live

- Browser-based web wallet launched, no download required.

## 2026-07-15 — Network Hashrate Crosses 1.17 GH/s

- First major hashrate milestone under civiclight v1.

## 2026-07-14 — Pool Infrastructure Stabilized

- New, more stable mining pool infrastructure deployed.

## 2026-07-10 — Mainnet Launch

- CivicNet (CIVIC) mainnet launched, running civiclight v1.
- 1% developer allocation (3,150,000 CIVIC) created at block 1, publicly disclosed from launch — reserved for core development, infrastructure, security improvements, and community bounties.
