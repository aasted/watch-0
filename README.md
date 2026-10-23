# watch-0

Learning to build watch faces for the **Pebble Core 2 Duo** (SDK platform `flint`:
144×168 black-and-white screen, 4 buttons).

## Projects

| Folder | What it is |
| --- | --- |
| [`hello-world/`](hello-world) | Minimal watch face: "Hello, World!" plus the time, updated every minute. |

## Build locally

```bash
# Install uv (https://docs.astral.sh/uv/), then:
uv tool install pebble-tool --python 3.13
pebble sdk install latest

cd hello-world
pebble build                          # -> build/hello-world.pbw
pebble install --emulator flint       # try it in the emulator
pebble install --phone <PHONE_IP>     # send to the watch (enable Developer Connection in the Pebble app)
```

You don't need a local setup: every push runs `.github/workflows/build.yml`, which builds
each project and uploads the `.pbw` (and an emulator screenshot when it works) as a run
artifact. Open the `.pbw` on your phone with the Pebble app to install it.
