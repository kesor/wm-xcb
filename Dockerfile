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

# Final image is based on nixos/nix so we have shell, Xvfb, x11vnc
FROM nixos/nix:latest

WORKDIR /wm

# Copy /nix/store
COPY --from=builder /tmp/nix-store-closure /nix/store
COPY --from=builder /tmp/build/result /wm

# Run with Xvfb and x11vnc for VNC access on port 5900
CMD ["/wm/bin/start.sh"]