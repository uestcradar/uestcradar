ARG WEB_PLATFORM=linux/arm64
ARG GO_BASE=registry.chengyistudio.com/cxx/web:build-base
FROM --platform=${WEB_PLATFORM} ${GO_BASE} AS builder

WORKDIR /src/web

# 配置国内 GOPROXY 代理，解决下载依赖包超时问题
ENV GOPROXY=https://goproxy.cn,direct

COPY workspace/web/go.mod workspace/web/go.sum ./
RUN go mod download

COPY workspace/web/frontend/package.json workspace/web/frontend/package-lock.json ./frontend/
RUN cmp frontend/package-lock.json /opt/web-frontend/package-lock.json \
    && cp -a /opt/web-frontend/node_modules frontend/node_modules

COPY workspace/web/ ./
COPY workspace/proto/telemetry.proto /src/proto/telemetry.proto
RUN (cd frontend && npm run build) \
    && protoc \
      -I /src/proto \
      --go_out=. \
      --go_opt=module=uestcradar/telemetry \
      /src/proto/telemetry.proto \
    && CGO_ENABLED=0 GOOS=linux GOARCH=arm64 go build -buildvcs=false -trimpath -ldflags="-s -w" \
       -o /out/telemetry ./cmd/telemetry

FROM --platform=${WEB_PLATFORM} scratch

LABEL io.uestcradar.contract="web/v1"

COPY --from=builder /out/telemetry /telemetry

USER 65532:65532

ENTRYPOINT ["/telemetry"]
