# Builds the btoon CLI tools in a small image.
#   docker build -t btoon .
#   echo '{"a":[1,2,3]}' | docker run --rm -i btoon encode - - | docker run --rm -i btoon decode - -
FROM ubuntu:24.04 AS build
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake ninja-build zlib1g-dev liblz4-dev libzstd-dev libbrotli-dev libsnappy-dev pkg-config \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY . .
RUN cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=OFF -DBUILD_EXAMPLES=OFF \
        -DBTOON_WITH_LZ4=ON -DBTOON_WITH_ZSTD=ON -DBTOON_WITH_BROTLI=ON -DBTOON_WITH_SNAPPY=ON \
    && cmake --build build \
    && cmake --install build --prefix /opt/btoon

FROM ubuntu:24.04
RUN apt-get update && apt-get install -y --no-install-recommends zlib1g liblz4-1 libzstd1 libbrotli1 libsnappy1v5 \
    && rm -rf /var/lib/apt/lists/*
COPY --from=build /opt/btoon/bin/ /usr/local/bin/
ENTRYPOINT ["btoon"]
CMD ["--help"]
