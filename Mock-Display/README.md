# Mock-Display

Persisted headless render output - the ESP32-S3 equivalent of InkyPiZero's
own `mock_display_output/` folder (see that project's `--mock-output`
flag). Every test/preview screen generated while working on this repo's
rendering pipeline lands here, timestamped, and **stays here** - nothing in
this folder is auto-deleted. That makes it a running visual history you can
scroll back through to see how the render has changed over time, not just a
snapshot of "the current one."

What ends up here:

- **`tools/dev_server.py`** - when its `/image` route has nothing real to
  show yet, it generates a placeholder (palette swatches + instructions)
  and saves it here instead of overwriting a single fixed file. Run it with
  `--snapshot` to generate one on demand without starting the server.
- **`tools/dump_to_png.py`** - reassembles a real `SerialDumpDriver` capture
  from a physical device into a PNG here by default (pass `--out` to
  override).

Filenames are timestamped (`<prefix>_YYYYMMDD_HHMMSS.png`, with a numeric
suffix appended on the rare collision) so repeated runs never clobber each
other.
