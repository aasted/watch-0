# Agent guidelines

Watch faces for the **Pebble Core 2 Duo** (SDK platform `flint`: 144×168, 1-bit black and
white, 4 buttons), written in C against the Pebble SDK.

## Always get a second review

Every pull request must be reviewed by **another agent** before it is merged. The agent that
wrote the change does not review it.

- **The review:** start a separate agent and have it post its review on the PR as review
  comments.
- **Findings:** address every finding. Either fix it, or reply on the thread saying why it
  stays as is. Then resolve the thread.
- **Merging:** merge only once the review is handled and CI is green on the latest commit.

## Layout

- One folder per face (`hello-world/`, `minimal-pt/`, `elevacao/`, ...). Each folder is a
  standalone SDK project with `package.json`, `wscript` and `src/c/<name>.c`.
- **`targetPlatforms`:** `["flint"]`.
- **`sdkVersion`:** `"3"`.
- **UUID:** give each face its own.
- `screenshots/<face>.png` holds the README gallery images.

## Adding a face

1. Copy an existing face's `wscript`. It silences the linker's RWX-segment warning, which is
   expected for Pebble apps.
2. Add the folder to the `project` matrix in `.github/workflows/build.yml`.
3. Add a row at the **top** of the README gallery (newest first). Also add
   `screenshots/<face>.png`, captured at the fixed CI time `1793095740` (Tue 27 Oct 2026,
   10:09). The `gallery` job keeps the images up to date after each merge to `main`.
4. Portuguese text is part of the house style: weekdays and months are hard-coded, not taken
   from `strftime`.

## Build and check

```bash
cd <face>
pebble build                                      # must finish with no warnings
pebble install --emulator flint --vnc build/<face>.pbw
pebble emu-set-time --emulator flint --vnc 1793095740   # after install: installing resets the clock
pebble screenshot --emulator flint --vnc --no-open shot.png
```

- **Look at the screenshot** before you push.
- **Fresh emulator per face:** run `pebble kill` and `pebble wipe` between faces. Installing
  one app over another within a few seconds makes the emulator show a spurious "not
  responding" screen.
- **Containers without IPv6:** pebble-tool's phone simulator fails to bind, and installs
  report "Connection refused". The local workaround is to change `("", self.port)` to
  `("0.0.0.0", self.port)` in pebble-tool's `pypkjs/runner/websocket.py`.

## Code conventions

- **Day changes:** redraw the date when the calendar day differs from the one shown. Don't
  rely on `DAY_UNIT`, because it is not set when the clock goes backwards.
- **Pixel drawing:** when drawing pixels directly, capture the frame buffer once per redraw
  and look rows up once (see `elevacao`). Calling SDK functions per pixel is slow on the
  watch.
- **Comments:** keep them short and explain *why*.
