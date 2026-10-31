# Real touch qualification on 0459

The user's real five-point taps, horizontal and vertical swipes, two-finger
separation, and sequential release reached the original IOHID input handler.
No firmware update, synthetic contact, controller reflash or filesystem change
was used for this test. Native/source remain the published 0459 checkpoint.

The capture starts with 156 previously produced/accepted reports and finishes
with 2,186 produced/accepted reports: 2,030 new physical reports during the
capture, zero reported queue drops and final result 0. The original device,
interface and IOHIDEventDriver registry matches are present. A direct report at
count 2,048 has `active=0x3` and `original-result=0` while both actual fingers
are down; it is not inferred from device registration alone.

## Five points and axes

The user confirmed this order with the phone upright and front camera at top
left. These are decoded sensor and existing HID logical coordinates, not an
optical measurement of where a GUI draws its buttons.

| Physical target | Sensor X,Y | HID logical X,Y |
| --- | --- | --- |
| Top left | 95,184 | 117,202 |
| Top right | 997,152 | 1019,170 |
| Bottom left | 96,2274 | 118,2292 |
| Bottom right | 983,2287 | 1005,2305 |
| Center | 495,1225 | 517,1243 |

Both axes follow the user's direction. Existing transform maps the identified
1079/2399 sensor maxima to a 1080x2400 panel and centers that panel in the
1125x2436 logical surface with +22/+18 offsets. The horizontal swipe covers
sensor X 10 -> 1068; the vertical swipe covers Y 42 -> 2381. Printed movement
is throttled to roughly 200 ms: the 178 decoded diagnostic event lines are not
all controller packets and do not measure physical sample rate.

## Contact lifetime and original HID

Fourteen complete contact lifetimes were observed, including three additional
single contacts between the requested point and swipe sequences. Every printed
down has a corresponding up, no slot is reused while active, and the final
tracked set is empty. Two overlap intervals use distinct IDs 0 and 1. On each,
ID 0 lifts first; ID 1 continues and lifts separately. The last interval holds
both contacts for 10.680 seconds before the first lift. This checks real slot
identity and release behavior; it is not a replayed fixture.

`0459-touch-live.log` is separately sealed from the startup/crypto regression
capture. SHA-256:
`f2b89ba4f1db98a7a23215927db50118b9af6c80f0385a9c2028148c217ab7f1`.
The final enabled original watchdog observation is polls1110/beats112. No
panic, HID failure or reported queue loss appears in this capture. The watchdog
remains the original software deadline monitor, not an independent hardware WDT.

The portable verifier binds the log to its capture provenance, exact native
hash, explicit user acknowledgements, compiled-source manifest and driver
source hashes; it checks the actual coordinates/lifetimes, report counters and
real simultaneous-contact success. It generates no input and writes no device.

## Scope that remains open

Physical input is qualified through the native report producer and original
`IOHIDDevice::handleReportWithTime` success. Visible SpringBoard gesture/event
delivery, pixel-perfect visual alignment and presentation are not qualified.
The descriptor's capacity is ten contacts, but this session physically tests
at most two. The panel target is 120 Hz; a 5 ms software input poll and these
HID counters do not establish real panel vsync or sensor sample frequency.
CoreLocation migration/system-shell check-in and usable SpringBoard remain
open. Bluetooth remains deferred until the GUI milestone.
