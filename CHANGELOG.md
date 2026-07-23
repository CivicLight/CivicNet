# CivicNet Changelog

This file is an append-only record of project milestones, protocol changes, and public commitments. Entries are added via git commit, not edited in place — the git history itself preserves the original text and timestamp of every entry.

---

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