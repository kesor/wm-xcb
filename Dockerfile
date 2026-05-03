# Build and run wm using Nix
FROM nixos/nix:latest

# Enable flakes
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
RUN nix develop --command make clean && \
    nix develop --command make LDFLAGS_STATIC=

# Run for testing
CMD ["sh", "-c", "Xvfb :0 -screen 0 1024x768x24 & sleep 2 && ./wm"]