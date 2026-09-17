# Guarded VP9 dimension admission probe

`tests/dimension-hardware.sh` checks the selected AVD userspace build through
public VA entrypoints. It is a manual hardware test, outside ordinary CI.
Use an exclusive healthy-device window and the repository hardware guard:

```sh
python3 tests/hwguard.py --identity avd --deadline 120 \
  --log /absolute/new-dimension-guard.jsonl -- \
  sh tests/dimension-hardware.sh /absolute/build/src /absolute/new-results
```

The output directory must not already exist. The helper requires libva/libva-drm
development files and a C compiler. It selects `v4l2_request` explicitly and checks
the loaded library path against the requested build. Record that build's source
commit, kernel/device identity and the generated driver/helper SHA-256 values.

For each of VP9 profiles 0 and 2, the probe checks the AVD contract of 64..4096
for both axes. Five unsupported size pairs must fail context creation before
format configuration or request allocation; 64x64 and 66x66 are positive context
admission controls. The same seven pairs are checked as picture parameters
inside a valid context. The 30 records comprise two attribute cases and 28
context/picture cases, followed by a summary.

The executable exports a transparent `ioctl` observer, forwarding arguments and
return values unchanged. Actual enumeration, format setup and request allocation
must be observed in the positive controls, establishing that the observer is
active. Initial driver-discovery control-capability probes are recorded separately;
the admission cases may not submit further codec controls or any media requests. Even accepted
picture parameters have no slice data, so ending those incomplete pictures must
fail. The helper does not inject a firmware fault or attempt to decode an
unsupported-size stream.

This proves admission and the owning rejection layer on the selected device,
not successful decoding of every admitted size. Full uninstrumented codec runs
and exact r11 pass-set comparisons remain a separate acceptance requirement for
issue #79. It does not establish a firmware minimum, resolution-change support,
generic-backend qualification or a new codec pass count.

Stop after a new kernel/decoder fault, wedge or foreign decoder client. Do not
advance the journal boundary merely to suppress a fault. Any separately approved
recovery must preserve the fault and its provenance and establish a fresh healthy
state before another test starts.
