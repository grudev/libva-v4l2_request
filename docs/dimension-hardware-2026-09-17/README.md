# M1 dimension-contract hardware validation — 17 September 2026

The merged source `299d29333bc215d9d045130a0e0c6b8558a6654a` passes the
selected-driver acceptance gate for [#79](https://github.com/iconidentify/libva-v4l2_request/issues/79).
The release-build driver SHA-256 is
`ce5c4513e72d1178f3bcb695f8acd76ad298564df8077eb408722cc447fb6f63`.
No production source was changed for this validation.

The [machine-readable report](validation.json) records the build, module recovery,
commands, guard outcomes and complete comparison verdicts.
[Raw evidence](raw-evidence.tar.xz) includes every conformance result, frame hash
and diagnostic log. Its [manifest](archive-manifest.json) identifies every original
and redacted member by SHA-256. Only the workspace prefix is redacted; no media,
executables or core dump are redistributed.

## Results

The [public-VA hardware probe](../dimension-hardware-probe.md) passed both VP9
profiles 0 and 2: two attribute checks and 28 context/picture admission cases.
The reported axes are 64..4096. Unsupported contexts reject before format setup
or request allocation; unsupported picture parameters reject without submission.
64x64 and 66x66 remain admissible. Across the admission cases, no codec controls
or media requests were submitted. Two initialization control-capability probes
are recorded separately. The positive context controls establish that the ioctl
observer sees actual device enumeration, setup and request allocation.

The first probe attempt failed because its counter included those initialization
controls. Its logs are preserved as a harness failure. The corrected probe ran
afresh; neither attempt produced a new kernel fault or left a decoder holder.

Uninstrumented generated checks passed 864 exact hardware frame comparisons:
336 mixed-codec shared-display frames, 144 H.264 High 10 frames and 384 VP9
frames, including normal and early export. Native-size/crop checks passed and
the truncated-input case rejected as expected.

| Suite / mode | Actual hardware passes | Exact r11 passing set |
| --- | --- | --- |
| HEVC JCT-VC-HEVC_V1 | 144/147 | unchanged |
| AVC JVT-AVC_V1 | 73/135 | unchanged |
| FRExt with FFmpeg High 10 compatibility | 27/69 | unchanged |
| VP9 VP9-TEST-VECTORS | 216/305 | unchanged |
| VP9 high-depth selected 10-bit 4:2:0 vector | 1/1 selected | unchanged; full suite has six entries |
| Explicit H.264 profile-override subset | 5/5 selected | unchanged; separate from default AVC |

The AVC strict comparator remains **non-green** for `FM1_FT_E`: it attempts
software fallback, which the hardware checker rejects and never counts as a pass.
This is the same recorded limitation as the previous qualification. HEVC's two
known wrong-output cases retain their actual checksums and category differences
from the coarse r11 record. No failing vectors are omitted to improve a total.

All three accepted guards ended healthy and idle, with no new fault, wedge,
timeout or foreign holder. The separately selected release build also passed
177 offline tests with actual TSan execution. PR #80 records the preceding
ASan/UBSan validation of the same production source.

## Recovery, scope and review

Initial preflight found a video client and an earlier decoder timeout at
2026-09-17 01:25:32 UTC. That timeout's cause remains unknown. After the user
closed all video apps, saved work and expressly authorized one existing-module
unload/reload, recovery succeeded at 02:16:40 UTC under the exclusive host lease.
The prior faults remain in the report. The new journal boundary is tied to that
recorded recovery; it was not moved merely to omit a fault.

The machine is Apple M1 T8103, kernel `7.1.13-3-1-ARCH`, linux-asahi package
`7.1.13.asahi3-1`. The existing module file selected for the reload hashes to
`e50540e1d0fc48c7e0bf9b21ff028758bdb35f94aa7070af00de88e61e5c70e9`.
This is file/reload provenance, not a measured in-memory hash. The installed
userspace package stayed at `1.3.r11-2`; the tests selected the separate build.
No installation, kernel edit or reboot occurred.

The admission probe does not prove firmware support below 64 pixels or decoding
of every admitted size. Generic-backend fixtures remain offline evidence. VP9
resizing, wider formats, client/display behaviour, boot stability and #41's
hour-long resource qualification are separate work. No support row or codec
count is promoted.

This probe and evidence received maintainer self-review. PR #80 separately
records independent maintainer review of its contributor implementation and
self-review of its maintainer corrections. AI generated this evidence record
under the owner's authorization.
