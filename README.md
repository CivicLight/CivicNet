CivicNet Core integration/staging tree
=====================================

https://civiclight.xyz/

What is CivicNet?
------------------
CivicNet (CIVIC) is a decentralized, community-driven cryptocurrency built
around one core idea: mining should stay accessible to ordinary hardware,
not become an arms race between specialized ASIC farms. CivicNet Core is
the open-source software that runs the network — a full node, wallet, and
consensus implementation forked from Litecoin Core 0.21.4.

CivicNet uses **civiclight**, a memory-hard, CPU-mineable proof-of-work
algorithm. The network hard-forked to civiclight v2 on July 23, 2026 to
close an ASIC-resistance gap identified in the original algorithm — see
[CHANGELOG.md](CHANGELOG.md) for the full history of network upgrades.

As of v3.0.0, CivicNet runs **Hybrid Proof-of-Work + Proof-of-Stake**
consensus. CPU mining and staking both produce blocks on the same chain:

- No minimum balance, no lock-up period, and no masternode tier required
  to stake
- Staking odds scale with the *square root* of a wallet's balance, not
  linearly — this keeps large holders from gaining outsized influence
- An anti-clustering rule prevents PoS blocks from crowding out mining
- Staking is gated by a hard-coded activation timestamp; see the release
  notes for the current network's activation date

Network parameters: 60-second block time, 77 CIVIC block reward, ~315
million CIVIC total supply cap. A 1% developer allocation (3,150,000
CIVIC) was created at block 1 and has been publicly disclosed since
mainnet launch, reserved for development, infrastructure, and community
bounties.

For downloads, the block explorer, and the web wallet, see
https://civiclight.xyz/.

License
-------
CivicNet Core is released under the terms of the MIT license. See
[COPYING](COPYING) for more information or see
https://opensource.org/licenses/MIT.

Development Process
--------------------
The `master` branch is regularly built and tested, but it is not
guaranteed to be completely stable. [Tags](https://github.com/CivicLight/CivicNet/tags)
are created for official, stable release versions of CivicNet Core.

The contribution workflow is described in
[CONTRIBUTING.md](CONTRIBUTING.md), and useful hints for developers can be
found in [doc/developer-notes.md](doc/developer-notes.md).

Testing
-------
Testing and code review is the bottleneck for development. Please be
patient and help out by testing other people's pull requests, and
remember this is a security-critical project where any mistake might cost
people real money.

### Automated Testing
Developers are strongly encouraged to write [unit tests](src/test/README.md)
for new code, and to submit new unit tests for old code. Unit tests can
be compiled and run (assuming they weren't disabled in configure) with:
`make check`. Further details on running and extending unit tests can be
found in [/src/test/README.md](/src/test/README.md).

There are also [regression and integration tests](/test), written in
Python, that can be run with: `test/functional/test_runner.py`

### Manual Quality Assurance (QA) Testing
Changes should be tested by somebody other than the developer who wrote
the code, especially for large or high-risk changes such as anything
touching consensus. It's useful to add a test plan to the pull request
description if testing the changes isn't straightforward.

Community
---------
- Website: https://civiclight.xyz
- GitHub: https://github.com/CivicLight
- Web Wallet: https://webwallet.civiclight.xyz
- Block Explorer: https://explorer.civiclight.xyz
- BitcoinTalk: see the pinned ANN thread for the latest updates
- Telegram: https://t.me/civiclight
- Twitter / X: https://x.com/civiclight_

Translations
------------
CivicNet does not currently run a dedicated translation platform.
Translation contributions are welcome as pull requests against the
`locale`/`qt/locale` files directly.
