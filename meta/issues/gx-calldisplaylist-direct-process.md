# Process GXCallDisplayList without the FIFO copy

## Summary

`GXCallDisplayList` (extern/aurora/lib/dolphin/gx/GXDispList.cpp) memcpys
the entire DL into the internal FIFO buffer, which is then re-parsed at
drain. Draining the pending buffer first and processing the DL bytes in
place (as `GXCallDisplayListLE` already does) would skip ~2.3 MB/frame of
redundant copying in gameplay scenes.

## Requirements

- Replace `fifo::write_data(data, nbytes)` with `fifo::drain()` +
  `fifo::process(data, nbytes, true)`.
- Confirm no ordering or re-entrancy hazards (a commented-out variant of
  exactly this already exists in the file marked "TEMP: debugging aid" —
  find out why it was disabled before enabling).

## Acceptance Criteria

- gx_bench trace replay unchanged or improved (note: traces capture at the
  process() level, so this needs a fresh capture or in-game measurement to
  show the win).
- Headless screenshot verification.

## Notes

- Small, cheap experiment (~0.1 ms/frame upper bound). Superseded if the
  static geometry cache lands first, since cached DLs skip the FIFO
  entirely.
