# Third-party notices — MPC DJ FX (`DjFx`)

This port reuses code from other projects. Files are vendored **byte-identical** under
`vst/third_party/` (hashes + exact commits in `vst/src/VENDORED.md`).

## Vendored / reused

- **WrongPeople/JustRepeat** — beat repeat / stutter DSP.
  <https://github.com/WrongPeople/JustRepeat> @ `0b09c50a08774c429164a407b5cd8d0092910e53`.
  License: **MIT**. Files: `JustRepeat.h`, `JustRepeat.cpp` (VST2 host glue dropped).
  © WrongPeople.

- **mda plugins** — through-zero flanger (`mdaThruZero`).
  Snapshot `mda-vst-src-2015-06-18.zip` (sha256 `88c8c9924cac8fd6bca6e1e82e71c38acec46491a5e53d47b193637c11288885`),
  as already vendored in the sibling port `../DubStation`. © mda / Paul Kellett.
  License: **MIT or GPL-2.0-or-later** (dual).
  *This port elects the **MIT** branch: `vst/src/flanger.c` is a rewrite of mda ThruZero's DSP
  (structure, delay-modulation shape and the dry-minus-wet through-zero mix) with our own controls,
  so the combined work stays MIT.*

- **DubStation DSP modules** (delay, resonant filter, plate/hall reverb, motion) — our own code from the
  sibling port `../DubStation`; the project is MIT. © the port authors.

- **sd88me/mpc-vst-plugins** — the porting framework (build wrapper, VST2 glue, skin tooling).
  <https://github.com/sd88me/mpc-vst-plugins> — MIT. Build dependency, not vendored here.

## Named but NOT used

- **dblue / Illformed “Glitch” v1.3** and **Glitch²** — freeware but **closed source**; not vendored.
- Ableton **`.adg`** racks — presets for Ableton’s closed devices; no DSP is portable from them.

## Licence of this port

**MIT.** Everything used is MIT: our own DubStation modules, `WrongPeople/JustRepeat`, and the **MIT
branch** of mda's dual licence. (An earlier draft said GPL-3.0-only because the plan assumed the mda
GPL-2 branch; the mda source is dual, and we take MIT.)
