# Disposable Linux host for end-to-end tests. Run ./tests/run.sh from the repo root.
FROM debian:bookworm
RUN apt-get update && apt-get install -y --no-install-recommends \
        gcc make libc6-dev libcap-dev libseccomp-dev uidmap slirp4netns iproute2 sudo ca-certificates \
    && rm -rf /var/lib/apt/lists/*
RUN useradd -m -s /bin/bash dev && chmod 700 /home/dev
COPY --from=alpine:3.20 --chown=dev:dev / /home/dev/rootfs-alpine
