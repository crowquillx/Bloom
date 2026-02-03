# Bloom - Containerized build environment
FROM archlinux:latest

# Pin package versions by using specific repo snapshot
# You can change this date to pin to a specific Arch snapshot
ARG REPO_DATE=2026/01/27

# Update mirrorlist to use archive for reproducibility
RUN echo "Server = https://archive.archlinux.org/repos/${REPO_DATE}/\$repo/os/\$arch" > /etc/pacman.d/mirrorlist

# Install build dependencies
RUN pacman -Sy --noconfirm archlinux-keyring && \
    pacman-key --init && \
    pacman-key --populate archlinux

RUN pacman -Syu --noconfirm && \
    pacman -S --noconfirm \
    base-devel \
    cmake \
    ninja \
    qt6-base \
    qt6-declarative \
    qt6-tools \
    qt6-multimedia \
    qt6-wayland \
    qt6-5compat \
    sqlite \
    mpv \
    git \
    libsecret \
    pkgconf


WORKDIR /workspace

# Add build script for convenience (supports incremental builds)
RUN printf '#!/bin/bash\n\
    set -e\n\
    echo "Building Bloom..."\n\
    \n\
    # Parse arguments\n\
    CLEAN_BUILD=false\n\
    while [[ "$#" -gt 0 ]]; do\n\
    case $1 in\n\
    --clean) CLEAN_BUILD=true ;;\n\
    *) echo "Unknown parameter: $1"; exit 1 ;;\n\
    esac\n\
    shift\n\
    done\n\
    \n\
    # Only clean if explicitly requested\n\
    if [ "$CLEAN_BUILD" = true ]; then\n\
    echo "Clean build requested, removing build-docker..."\n\
    rm -rf build-docker\n\
    fi\n\
    \n\
    mkdir -p build-docker\n\
    cd build-docker\n\
    \n\
    # Run cmake only if not already configured, or if CMakeLists.txt changed\n\
    if [ ! -f "build.ninja" ] || [ "../CMakeLists.txt" -nt "build.ninja" ]; then\n\
    echo "Running CMake configuration..."\n\
    cmake .. -G Ninja\n\
    else\n\
    echo "CMake already configured, skipping reconfiguration..."\n\
    fi\n\
    \n\
    echo "Running incremental build..."\n\
    ninja\n\
    echo ""\n\
    echo "Build complete! Binary at: build-docker/src/Bloom"\n' > /usr/local/bin/build-bloom.sh && \
    chmod +x /usr/local/bin/build-bloom.sh

# For development: mount your source code
# docker run -v $(pwd):/workspace -it bloom-build

CMD ["/bin/bash"]
