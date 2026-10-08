ARG GO_BASE=registry.chengyistudio.com/cxx/web@sha256:52c3755b78f07a64e28b95efccf3d4c70ac97808b65b51bc1e8f8a7829c2835b
FROM --platform=linux/arm64 ${GO_BASE} AS builder
WORKDIR /src/frontend
ENV GOPROXY=https://goproxy.cn,direct
RUN test "$(uname -m)" = aarch64
COPY workspace/infra/frontend/go.mod workspace/infra/frontend/go.sum ./
RUN go mod download
COPY workspace/infra/frontend/ui/package.json workspace/infra/frontend/ui/package-lock.json ./ui/
RUN cmp ui/package-lock.json /opt/web-frontend/package-lock.json \
    && cp -a /opt/web-frontend/node_modules ui/node_modules
COPY workspace/infra/frontend/ ./
RUN cd ui && npm run build

FROM builder AS test
RUN cd ui && npm test
RUN go test -count=1 ./... && go vet ./...

FROM test AS build
RUN CGO_ENABLED=0 GOOS=linux GOARCH=arm64 go build -buildvcs=false -trimpath -ldflags="-s -w" -o /out/frontend ./cmd/frontend

FROM --platform=linux/arm64 scratch
COPY --from=build /out/frontend /frontend
COPY --from=builder /etc/ssl/certs/ca-certificates.crt /etc/ssl/certs/ca-certificates.crt
USER 65532:65532
HEALTHCHECK --interval=5s --timeout=3s --start-period=5s --retries=3 CMD ["/frontend", "--healthcheck"]
ENTRYPOINT ["/frontend"]
