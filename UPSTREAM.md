# Dexed upstream provenance

Agentic Dexed is a derivative of [Dexed](https://github.com/asb2m10/dexed), distributed under the GNU General Public License version 3. The original `LICENSE` file and third-party notices remain part of this repository and every corresponding-source release.

The initial import is pinned to Dexed commit:

```text
2e182b3db85c09083ab13c8b9b00565ce7d9ff85
```

That commit records these top-level submodule revisions:

```text
libs/JUCE                         ae5144833e852815d61642af87c69b9db44984f7
libs/MTS-ESP                      803c3aab3d43dfaea430a1084ab31b606f5cd72c
libs/clap-juce-extensions         4d454e5125da75a0e75d95615cbec26d2a09e2bf
libs/surgesynthteam_tuningui      54f9a74cd55cdb33fb4d32d706067626857cfc75
libs/tuning-library               3bbe9514816e1ae674c207b09e9f20eea4df372a
libs/vst3sdk                      56e4b2a644be164c5d324e8bc9de55b964b0f102
```

Initialize the complete dependency tree with:

```bash
git submodule update --init --recursive
```

Upstream updates must arrive as explicit, reviewed merges from the `dexed-upstream` remote. Each update must name the selected Dexed commit, review upstream and submodule changes, update the revision list above, and pass the compatibility and build gates before it is accepted. Do not advance submodules independently of the reviewed upstream merge unless the change is documented as a deliberate fork dependency update.
