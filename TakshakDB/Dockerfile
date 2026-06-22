# Build stage
FROM alpine:3.18 AS builder

RUN apk add --no-cache build-base cmake linux-headers

WORKDIR /app
COPY . .

RUN mkdir -p build && cd build && \
    cmake .. -DCMAKE_BUILD_TYPE=Release && \
    make -j$(nproc)

# Run stage
FROM alpine:3.18

RUN apk add --no-cache libstdc++

WORKDIR /app
COPY --from=builder /app/build/takshak-server /app/takshak-server

# Expose the database port
EXPOSE 6379

# Create data directory for AOF persistence
RUN mkdir -p /app/data

# Run the server
CMD ["/app/takshak-server"]
