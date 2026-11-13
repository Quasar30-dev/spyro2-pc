# Character shadow quality R2, 2026-11-05

The user accepted the trailer edit but requested higher-quality shadows. The
renderer now uses a 4096 x 4096 map instead of 2048 x 2048, retaining the same
96-unit coverage. Ground texels shrink from 0.046875 to 0.0234375 units. Both
the viewport and the snapped projection anchor use the same resolution.

A separate comparison sampler filters visibility comparisons before blending
them. A weighted 1/2/1 PCF kernel gives smooth fractional coverage; retail art
continues to use its existing nearest sampler. Raw depths are not interpolated
into false ramps. The original animated silhouettes, depth union, 0.025-unit
receiver separation, 3-to-12-unit height fade and 46% maximum opacity remain.
The map uses 64 MiB instead of 16 MiB. Transparent water and separate splat
materials remain outside the receiving pass.

Compiled and installed main SHA-256:
`c3aa993f399a34aa8530f820fc88fed53cc019e82bbff0ba78e30ed238e103cc`.
`build/shadow-quality-20261105/build.log` records both main and test builds;
`install.log:2` records the deployed hash. No user process was closed.

`tests.log:28` records 226 focused checks passing. These include actual GPU
receiver darkening, slope/edge/gap handling, no overlapping darkening or sky
leaks, NPC/Spyro silhouettes, hidden exclusion and all 23 life jars. Separate
installed-main captures cover Glimmer, the intact jar and normal flame/charge
release with zero script faults. `captures.log:4` records the main identity.
`jar-charge.log` records the additional normal charge result. The Glimmer
capture reports 14 casters, 4,806 shadow triangles and 2.106 ms mean render time
on this host; this is a bounded capture measurement, not a general benchmark.

Matched images: `glimmer-comparison.png`, `glimmer-detail.png`,
`jar-comparison.png`, `jar-detail.png`, under that build directory. Their detail
crops use identical bounds and nearest enlargement to expose sampling changes.

The two R2 ZIPs in `dist/` contain this exact main. Their extracted contents are
hash verified and four hidden package launches pass in `package.log:7`.
Spring Realm retains the fused Blender source and previews. Package identities
are in `packages/summary.json` and `dist/spyro2-pc-LATEST.txt`.

The approved 79.221-second trailer edit is retained. Its gameplay reshoot used
the earlier validated shadow build, and this quality-only revision does not
change that film. Manual gameplay validation remains with the user.
