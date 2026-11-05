# Synthetic electron/proton examples

To generate a project from actual legacy trapped-belt model output, see the [AE8/AP8 orbit example](../../docs/AE8_AP8_EXAMPLE.md). Its orbit is idealized.

`spacecraft-tid-assembly.json` is a notional spacecraft with a bus, local housings and two template-derived solar wings. It opens with a placeholder synthetic spectrum. The [spacecraft TID benchmark](../../docs/SPACECRAFT_TID_BENCHMARK.md) documents replacement with model fluxes from a public trajectory, shielding scans and statistical screening.

`voxel-scoring.json` is a schema-v7, 2×2×2 synthetic TID example. It subdivides one homogeneous silicon sensitive box surrounded by an aluminum box shell. Use the [voxel workflow guide](../../docs/VOXEL_SCORING.md). It is intended for software verification only.

`shielding-scan.json` is a separate small Si sphere / Al box-shell example for the [automated scan workflow](../../docs/SHIELDING_SCAN.md). It uses the same synthetic spectra, a 40 mm source radius and 10,000 histories per incident species/thickness. The paired settings file is `examples/scans/three-thicknesses.json`. The two flux/fluence examples below retain their original three-target geometry and normalization.

Open either JSON with the desktop's Open Project action. Both use the built-in three-target analytic geometry and the same isotropic omnidirectional scalar environment. They contain deliberately simple numbers for software checks, not AP8/AE8, AE9/AP9 or a mission prediction.

| Particle | Energy MeV | Linear differential flux (cm-2 s-1 MeV-1) | Integral (cm-2 s-1) |
|---|---|---|---|
| proton | 10, 20, 30 | 1, 1, 1 | 20 |
| e- | 1, 2, 3 | 4, 2, 0 | 4 |

The flux project uses 64 s exposure. The equivalent fluence project multiplies every differential value by 64 and records the conversion; its exposure is context only. The fluence integral is 1536 cm-2. The source radius is 500 mm, so both normalize to `pi * 50^2 * 1536` expected primaries, about 12,063,715.7898. Sampling shares are proton 5/6 and electron 1/6.

The examples request 100,000 Monte Carlo histories, not that many physical primaries. Increase histories as required and inspect nonzero histories and RSE. Shielded/small targets may have zero hits; a zero estimate with unknown RSE does not establish a zero dose. No device qualification or independent physical validation is claimed.
