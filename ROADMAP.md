# Roadmap

Three milestones, in order. Each one is done when the stated result exists, not when the work starts.

## 1. A release you can install without a compiler

Today the only way to run this plugin is to build it: Docker, an arm32v7 toolchain chain and a
framework checkout. That excludes anyone who just wants to try the effect.

- One shareable zip: the plugin folder, `install.sh`, `uninstall.sh`, `INSTALL.md` and `SHA256SUMS`,
  built with the framework's `tools/release.py`.
- A signed-off device test on the artifact itself, not on a local build.
- A GitHub release for v1.0.0 with that zip attached.

Done when a person with an MPC and root SSH can install the release without building anything.

## 2. DjFx2 in this repository

The successor exists only as a working copy. It becomes a real release here.

- Grain stage that replaces the flanger stand-in at the macro's "glitch" position.
- True-peak limiter.
- "Fade to grey" as a band that moves across the spectrum instead of a shelf kill.
- Macro curves fitted to measurements of the original rack, using the A/B harness in `vst/tests`.

Done when DjFx2 has a release artifact and a device test, and the two versions can be compared on the
same device without sharing a uid or an `.so` name.

## 3. A device matrix instead of one device

Every claim so far comes from one MPC Live II on MPC OS 3.9.1.2.

- Test on a Force and on MPC One, Live and X, on both MPC OS 2.x and 3.x.
- Record each combination in `tested.json` with the firmware version and the date.
- Take one screenshot per device generation into `docs/`.
- State the MPC OS 2.x limits in the README next to the install steps, where the reader hits them.

Done when the README says which hardware and which firmware were actually tested, per row.
