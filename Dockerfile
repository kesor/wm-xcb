# Build and run wm using Nix
FROM nixos/nix:latest AS builder

# Enable flakes (git is already in nixos/nix image)
RUN mkdir -p /etc/nix && \
    echo 'experimental-features = nix-command flakes' >> /etc/nix/nix.conf

WORKDIR /src

# Copy source files
COPY . ./

# Remove any parent git references and init fresh repo
RUN rm -rf .git && \
    git init --initial-branch=main && \
    git config user.email "build@container" && \
    git config user.name "Build" && \
    git add . && \
    git commit -m "init"

# Build using nix develop shell environment (dynamic linking)
# Combine clean and build in single nix develop call for faster builds
RUN nix develop --command sh -c 'make clean && make LDFLAGS_STATIC='

# ---------------------------------------------------------------------------
# Runtime stage
# ---------------------------------------------------------------------------
FROM nixos/nix:latest

# Install Xvfb for headless testing
RUN nix-channel --add https://nixos.org/channels/nixos-unstable nixpkgs && \
    nix-channel --update && \
    nix-env -f '<nixpkgs>' -iA xvfb

WORKDIR /src

# Copy built binary from builder
COPY --from=builder /src/wm ./

# Run for testing
CMD ["sh", "-c", "Xvfb :0 -screen 0 1024x768x24 & sleep 2 && ./wm"]