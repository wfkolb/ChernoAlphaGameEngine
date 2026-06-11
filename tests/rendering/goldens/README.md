# Rendering Golden Images

This directory holds the reference PNG images used by `GoldenTests` (label: integration).

## How goldens are created

Golden files are **auto-generated** on the first successful test run on real GPU hardware.
When a golden file does not exist, `assertMatchesGolden()` saves the actual rendered output
as the new baseline and marks the test as passed.

Subsequent runs compare the actual output against the saved golden using per-channel RMSE.
The default threshold is **2% (0.02)** across R, G, and B channels.

## Regenerating a golden

1. Delete the stale PNG file from this directory.
2. Re-run the integration tests on a machine with a DX12-capable GPU and a display:
   ```
   cd build\debug
   ctest -L integration --output-on-failure
   ```
3. The missing golden is recreated automatically.

## Files

| File | Test | Description |
|------|------|-------------|
| `clear_red.png` | `GoldenTestFixture.ClearRed` | 256x256 solid red clear |

Binary PNG files are tracked in version control so that any machine with the repo
can verify renders without first running on GPU hardware.
