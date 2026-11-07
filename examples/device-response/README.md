# Device response screening table

The desktop DDD/SEE screen accepts a CSV with this exact header:

```text
device_id,metric,particle,energy_MeV,response,response_unit,angular_basis,reference
```

Use `angular_basis=isotropic_omnidirectional` to declare compatibility with the project source. Use `metric=ddd` with `response_unit=MeV_cm2_per_g` or `metric=see` with `response_unit=cm2_per_device`. Provide one strictly increasing energy curve for every sensitive project component and every incident project species. The curve must cover the complete source spectrum; the fold rejects extrapolation.

Set `angular_basis=isotropic_omnidirectional` to match the project source. The reference field is retained in the audit archive but is not independently authenticated. DDD is reported as NIEL energy per mass and its corresponding NIEL dose; SEE is expected events per device with an optional Poisson probability. These are deterministic response-table folds and do not simulate Geant4 DDD/DPA or SEE events.
