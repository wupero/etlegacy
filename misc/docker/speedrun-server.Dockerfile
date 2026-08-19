# syntax=docker/dockerfile:1

# =====================================================================
# etlegacy speedrun dedicated server - production image (all client platforms)
#
# Build:   docker compose up --build   (from the dir containing this file)
#   - clones wupero/etlegacy (source) + wupero/etl-speedrun-configs (client
#     modules + timeruns/mapscripts) using GITHUB_TOKEN (build-arg, read from
#     the VPS environment - see the compose file)
#   - builds ONLY the Linux server pieces natively in-image: etlded (dedicated
#     server) + qagame (server-side mod) for the image arch
#   - takes the prebuilt CLIENT modules for ALL platforms (mac, win x64+x86,
#     linux x86_64+aarch64) from the configs repo modules/ dir - they are built
#     on a dev machine (see README) and committed there, so this image does not
#     need to cross-compile cgame/ui at all
#   - packs speedrun.pk3 with the repo's etmain + all client modules
#   - pulls pak0/1/2 (ET 2.60b base game) and assembles the runtime mod dir
#
#   Keep the configs repo modules/ in sync whenever the cgame/ui source here
#   changes (rebuild + commit them - see the workflow note in this repo).
#
#   Repos are PRIVATE: GITHUB_TOKEN must be a repo-scoped token (Contents:
#   Read on wupero/etlegacy + wupero/etl-speedrun-configs). It is a build-arg,
#   visible in `docker history`.
#
# Run:     docker compose up -d
#   Starts on radar with the allruns config, g_gametype 3 (campaign). Runtime
#   cvars overridable via env (HOSTNAME, MAPS, PORT, RCON_PASSWORD, G_API_URL).
#   Joins the shared etl-speedrun-net so it can POST records to the speedrun
#   API (g_apiUrl -> http://api:8090/api by default).
# =====================================================================

# --- Build stage: toolchain + build the Linux server pieces ---------------
FROM debian:stable-slim AS build

# Tools needed to compile etlegacy's bundled libs + fetch assets.
RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        build-essential cmake ninja-build git \
        wget curl ca-certificates unzip zip pkg-config \
        nasm autoconf automake libtool make \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src

# Which refs to build. Default: latest of the default branches.
ARG REF=master
ARG CONFIG_REF=master
ARG GITHUB_TOKEN=

# Base game paks (pak0/1/2) are the only non-source download.
ARG ET_FULL_URL=https://cdn.splashdamage.com/downloads/games/wet/et260b.x86_full.zip

# --- 1. Clone the speedrun mod source (this repo) --------------------------
RUN if [ -n "${GITHUB_TOKEN}" ]; then \
        git clone --depth 1 "https://x-access-token:${GITHUB_TOKEN}@github.com/wupero/etlegacy.git" /src/etlegacy; \
    else \
        GIT_SSH_COMMAND='ssh -o StrictHostKeyChecking=accept-new' \
        git clone --depth 1 git@github.com:wupero/etlegacy.git /src/etlegacy; \
    fi \
    && git -C /src/etlegacy checkout "${REF}" \
    && git -C /src/etlegacy rev-parse --short HEAD > /src/.git-short-sha

# --- 2. Clone the runtime configs repo --------------------------------------
# Holds the prebuilt client modules (modules/), timerun definitions
# (luascripts/ -> timeruns/) and mapscripts/. All are needed at runtime.
RUN if [ -n "${GITHUB_TOKEN}" ]; then \
        git clone --depth 1 "https://x-access-token:${GITHUB_TOKEN}@github.com/wupero/etl-speedrun-configs.git" /src/configs; \
    else \
        GIT_SSH_COMMAND='ssh -o StrictHostKeyChecking=accept-new' \
        git clone --depth 1 git@github.com:wupero/etl-speedrun-configs.git /src/configs; \
    fi \
    && git -C /src/configs checkout "${CONFIG_REF}"

# --- 3. Build the Linux server pieces natively -----------------------------
# Only etlded + qagame (server). BUILD_CLIENT_MOD=OFF: the cgame/ui client
# modules come prebuilt from the configs repo, so none are compiled here.
# Configure still generates version_generated.h (needed by the pk3's ui/).
ARG TARGETARCH
WORKDIR /src/etlegacy/build
RUN cmake .. -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_CLIENT=OFF \
        -DBUILD_SERVER=ON \
        -DBUILD_MOD=ON \
        -DBUILD_CLIENT_MOD=OFF \
        -DBUILD_SERVER_MOD=ON \
        -DBUILD_MOD_PK3=OFF \
    && ninja etlded qagame

# --- 4. Fetch base game paks (pak0/1/2) from ET 2.60b -----------------------
# The installer zip only carries the keygen .run; the paks are inside it, so it
# must be self-extracted (`sh <keygen> --tar xf`) like the reference does.
RUN mkdir -p /legacy/server/etmain && \
    curl -SL "${ET_FULL_URL}" -o /tmp/et_full.zip && \
    cd /tmp && unzip -q et_full.zip && \
    chmod +x /tmp/et260b.x86_keygen_V03.run && \
    sh /tmp/et260b.x86_keygen_V03.run --tar xf && \
    cp /tmp/etmain/pak*.pk3 /legacy/server/etmain/ && \
    rm -rf /tmp/et_full.zip /tmp/et260b.x86_keygen_V03.run /tmp/etmain

# --- 5. Assemble the speedrun mod dir ---------------------------------------
# Mod dir name = fs_game value ("speedrun"). Loose qagame (server mod) + the
# all-platform speedrun.pk3 (clients download it) + timeruns/mapscripts from
# the configs repo (loose overrides the pk3 copies via fs search order).
RUN set -e; \
    # module arch suffix for the server-side qagame (TARGETARCH arm64 -> aarch64)
    if [ "$TARGETARCH" = "arm64" ]; then NATIVE=aarch64; else NATIVE=x86_64; fi; \
    \
    mkdir -p /legacy/server/speedrun && \
    cp /src/etlegacy/build/legacy/qagame.mp.${NATIVE}.so /legacy/server/speedrun/qagame.mp.${NATIVE}.so && \
    \
    # timerun definitions: configs repo keeps them under luascripts/, the
    # server loads them from timeruns/<mapname>.lua
    mkdir -p /legacy/server/speedrun/timeruns /legacy/server/speedrun/mapscripts && \
    cp /src/configs/luascripts/*.lua /legacy/server/speedrun/timeruns/ && \
    cp /src/configs/mapscripts/*.script /legacy/server/speedrun/mapscripts/ && \
    \
    # speedrun.pk3 = repo etmain/* + the ui version header + all client modules
    mkdir -p /tmp/pk3 && cd /tmp/pk3 && \
    cp -r /src/etlegacy/etmain/. . && \
    cp /src/etlegacy/build/etmain/ui/version_generated.h ui/version_generated.h && \
    cp /src/configs/modules/* . && \
    zip -qr /legacy/server/speedrun/speedrun.pk3 . && \
    rm -rf /tmp/pk3

# --- 6. Runtime entrypoint ---------------------------------------------------
# Overridable via env (HOSTNAME, MAPS, PORT, RCON_PASSWORD, G_API_URL).
# g_apiUrl is CVAR_ARCHIVE and is exec'd from /legacy/homepath/etconfig_server.cfg
# at boot, so a stale archived value would override the command line - strip it
# first so the env value always wins. G_API_URL points at the speedrun API on the
# shared etl-speedrun-net (api:8090). The allruns config forces sv_pure 1 /
# g_gameType 3.
RUN printf '%s\n' \
    '#!/bin/sh' \
    'set -e' \
    'cd /legacy/server' \
    '# drop any stale archived g_apiUrl so the env value below always wins' \
    'sed -i "/seta g_apiUrl/d" /legacy/homepath/etconfig_server.cfg 2>/dev/null || true' \
    'exec ./etlded +set dedicated 2 +set fs_basepath /legacy/server +set fs_homepath /legacy/homepath +set fs_game speedrun +set g_customConfig allruns +set sv_hostname "${HOSTNAME:-ETLHost}" +set net_port "${PORT:-27960}" +set rconPassword "${RCON_PASSWORD:-}" +set g_apiUrl "${G_API_URL:-http://api:8090/api}" +map radar}"' \
    > /legacy/server/entrypoint.sh && chmod +x /legacy/server/entrypoint.sh

# --- Runtime stage: minimal, non-root --------------------------------------
FROM debian:stable-slim

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        ca-certificates curl \
    && rm -rf /var/lib/apt/lists/* \
    && useradd -Ms /bin/bash legacy \
    && mkdir -p /legacy/homepath

COPY --from=build --chown=legacy:legacy /legacy /legacy/

WORKDIR /legacy/server

# Game data + mod live in the image; the homepath holds runtime writes
# (logs, configs, demo downloads) and can be mounted.
VOLUME /legacy/homepath

EXPOSE 27960/udp

USER legacy

ENTRYPOINT ["./entrypoint.sh"]
