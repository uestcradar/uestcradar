ARG WEB_PLATFORM=linux/arm64
ARG GO_BASE=registry.chengyistudio.com/cxx/telemetry-web:build-base
FROM --platform=${WEB_PLATFORM} ${GO_BASE} AS builder

WORKDIR /src/web

# 配置国内 GOPROXY 代理，解决下载依赖包超时问题
ENV GOPROXY=https://goproxy.cn,direct

COPY workspace/web/go.mod workspace/web/go.sum ./
RUN go mod download

COPY workspace/web/ ./
COPY workspace/proto/telemetry.proto /src/proto/telemetry.proto
RUN protoc \
      -I /src/proto \
      --go_out=. \
      --go_opt=module=uestcradar/telemetry \
      /src/proto/telemetry.proto \
    && CGO_ENABLED=0 GOOS=linux GOARCH=arm64 go build -buildvcs=false -trimpath -ldflags="-s -w" \
       -o /out/telemetry ./cmd/telemetry

FROM --platform=${WEB_PLATFORM} scratch

COPY --from=builder /out/telemetry /telemetry

ENTRYPOINT ["/telemetry"]
