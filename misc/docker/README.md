# speedrun server Docker image — building/deploying

Build + run (Linux VPS, Docker + docker compose) — from the REPO ROOT:

    export GITHUB_TOKEN=<repo-scoped token>   # or a .env file
    docker compose up -d --build

The image builds `etlded` + `qagame` (Linux server) from wupero/etlegacy and
takes the prebuilt cgame/ui client modules for ALL platforms (mac, win x64+x86,
linux x86_64+aarch64) plus the timeruns/mapscripts from
wupero/etl-speedrun-configs (the `modules/`, `luascripts/`, `mapscripts/` dirs).

## Keeping the client modules in sync (IMPORTANT workflow)

`modules/` in the etl-speedrun-configs repo holds prebuilt client binaries, so
the Docker image does not compile cgame/ui. Whenever you change the CLIENT-side
`cgame`/`ui` source in THIS (etlegacy) repo, you must rebuild the modules and
push them to the configs repo before the image reflects the change:

  # 1. native macOS modules (on a Mac, arm64)
  cmake --build cmake-build-arm64 --target cgame ui
  cp cmake-build-arm64/legacy/cgame_mac cmake-build-arm64/legacy/ui_mac \
      ~/Projects/etl-speedrun-configs/modules/

  # 2. Windows + Linux modules (cross-compiled in the etlegacy-cross container)
  docker exec etlegacy-cross bash -lc 'cd /code && \
    cmake --build build-win64 --target cgame ui && \
    cmake --build build-win32 --target cgame ui && \
    cmake --build build-linux64 --target cgame ui && \
    cmake --build build-linux-arm64 --target cgame ui'
  cp build-win64/legacy/cgame_mp_x64.dll build-win64/legacy/ui_mp_x64.dll \
      build-win32/legacy/cgame_mp_x86.dll build-win32/legacy/ui_mp_x86.dll \
      build-linux64/legacy/cgame.mp.x86_64.so build-linux64/legacy/ui.mp.x86_64.so \
      build-linux-arm64/legacy/cgame.mp.aarch64.so build-linux-arm64/legacy/ui.mp.aarch64.so \
      ~/Projects/etl-speedrun-configs/modules/

  # 3. commit + push the configs repo, then rebuild the image.

Server-side (qagame), config, mapscript and timerun changes do NOT touch the
client modules - only the etlegacy repo source (qagame) and the configs repo
(luascripts/mapscripts) matter, and both are cloned fresh each build.

## Deploying to a VPS

Copy `Dockerfile` + `docker-compose.yml` (repo root) to the VPS (those two are enough;
no mac/ context needed anymore), set GITHUB_TOKEN in the environment, and and run
`docker compose up -d --build` from that directory. Game UDP port 27960 is published; logs
and configs persist in the named `etl-speedrun-homepath` volume.

## Talking to the speedrun API

The server POSTs records to the backend (g_apiUrl). It joins the SAME external
network as etl-speedrun-api / etl-speedrun-web so it can reach the api by
service name. Create that network once (all stacks share it):

    docker network create etl-speedrun-net

`G_API_URL` (runtime env, default `http://api:8090/api`) sets g_apiUrl at
startup; the entrypoint also strips any stale archived g_apiUrl from the
homepath `etconfig_server.cfg` so the env value always wins.
