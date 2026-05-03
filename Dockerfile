# Nix builder
FROM nixos/nix:latest AS builder

# Copy our source to /tmp/build
COPY . /tmp/build

WORKDIR /tmp/build

# Remove any parent .git references and init fresh repo for flake
RUN rm -rf .git && \
    git init && \
    git config user.email "build@container" && \
    git config user.name "Build" && \
    git add . && \
    git commit -m "init"

# Build our Nix environment
RUN nix \
    --extra-experimental-features "nix-command flakes" \
    build

# Copy the Nix store closure into a directory. The Nix store closure is the
# entire set of Nix store values that we need for our build.
RUN mkdir /tmp/nix-store-closure && \
    cp -R $(nix-store -qR result/) /tmp/nix-store-closure

# Final image is based on scratch. We copy a bunch of Nix dependencies
# but they're fully self-contained so we don't need Nix anymore.
FROM ubuntu:24.04

WORKDIR /wm

# Install Xvfb for headless testing
RUN apt-get update && apt-get install -y --no-install-recommends xvfb && rm -rf /var/lib/apt/lists/*

# Copy /nix/store
COPY --from=builder /tmp/nix-store-closure /nix/store
COPY --from=builder /tmp/build/result /wm

# Run with Xvfb for headless testing
CMD ["sh", "-c", "Xvfb :0 -screen 0 1024x768x24 & sleep 1 && DISPLAY=:0 /wm/bin/wm"]