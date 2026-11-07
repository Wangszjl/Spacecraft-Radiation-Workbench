# Synthetic ephemeris fixture

`synthetic-stationary-geo-km.csv` is an artificial stationary position at GEO
(2 Re, 0, 0), using 6371.2 km/Re. The timestamps span 680.896 seconds and include
unequal intervals. It exercises input parsing, time integration and model
execution. It is not a propagated orbit, flight ephemeris or qualified mission.

Select GEO, km, maximum gap 60 seconds and explicitly choose the electron and
proton energy bands. Model outputs are subject to the same invalid-data rejection
as other inputs. SpacePy 0.7.0 is configured separately.
