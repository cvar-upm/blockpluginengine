<!-- SPDX-FileCopyrightText: 2026 Universidad Politécnica de Madrid (UPM) - CVAR-UPM -->
<!-- SPDX-License-Identifier: BSD-3-Clause -->

# BPE 1.0 release checklist

- [ ] Confirm institutional authority to release under BSD-3-Clause and confirm third-party provenance; the declared institutional holder is UPM (CVAR-UPM).
- [ ] Review third-party/copy-derived sources and embedded diagnostic examples
      for any additional copyright and license notices.
- [ ] Confirm the 1.0.0 version is intended for **all five** ROS 2 packages.
- [ ] Build a clean workspace; run the full CTest suite, including the legacy
      `ITimerBlock` pluginlib compatibility test.
- [ ] Build and run all four YAML-based examples from the installed workspace.
- [ ] Check installed headers: `IScheduledBlock` and deprecated `ITimerBlock`
      public; `ITimerBlockWithInfo` private.
- [ ] Verify the distribution with `reuse lint` if the REUSE CLI is available.
- [ ] Rebuild downstream plugins against BPE 1.0; aliasing preserves source
      compatibility but does not promise binary compatibility.
- [ ] Review `AI_USAGE.md` for accuracy and project release approval.

## Deferred internal change

`ITimerBlockWithInfo` remains used by production Fair and Hybrid scheduler
implementations and by timing tests. It is private for 1.0, but its internal
removal/refactoring is postponed rather than performed as part of this release.
