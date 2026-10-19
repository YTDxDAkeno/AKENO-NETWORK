# syntax=docker/dockerfile:1
#
# AKENO voice gateway image (apps/voice-gateway): lets paired consoles take part in LiveKit party
# voice (docs/PS5_VOICE.md). Debian-based because LiveKit's Node SDK ships glibc builds of its
# native library only. Build from the repository root:
#   docker build -f infrastructure/docker/voice-gateway.Dockerfile -t akeno-voice-gateway .

ARG NODE_IMAGE=node:22-bookworm-slim

FROM ${NODE_IMAGE} AS build
ENV CI=true
RUN --mount=type=secret,id=extra_ca,required=false \
    if [ -s /run/secrets/extra_ca ]; then export NODE_EXTRA_CA_CERTS=/run/secrets/extra_ca; fi; \
    npm install --global pnpm@10.28.0
WORKDIR /repo
COPY package.json pnpm-lock.yaml pnpm-workspace.yaml ./
COPY apps/voice-gateway/package.json apps/voice-gateway/
RUN --mount=type=secret,id=extra_ca,required=false \
    if [ -s /run/secrets/extra_ca ]; then export NODE_EXTRA_CA_CERTS=/run/secrets/extra_ca; fi; \
    pnpm install --frozen-lockfile --filter @akeno/voice-gateway...
COPY tsconfig.base.json ./
COPY apps/voice-gateway apps/voice-gateway
RUN pnpm --filter @akeno/voice-gateway run build
RUN --mount=type=secret,id=extra_ca,required=false \
    if [ -s /run/secrets/extra_ca ]; then export NODE_EXTRA_CA_CERTS=/run/secrets/extra_ca; fi; \
    pnpm --filter @akeno/voice-gateway deploy --prod --legacy /deploy

FROM ${NODE_IMAGE} AS runtime
ENV NODE_ENV=production GATEWAY_HOST=0.0.0.0 GATEWAY_PORT=7890
WORKDIR /app
COPY --from=build --chown=node:node /deploy/package.json ./package.json
COPY --from=build --chown=node:node /deploy/node_modules ./node_modules
COPY --from=build --chown=node:node /repo/apps/voice-gateway/dist ./dist
USER node
EXPOSE 7890
HEALTHCHECK --interval=30s --timeout=5s --start-period=20s --retries=3 \
  CMD node -e "fetch('http://127.0.0.1:7890/healthz').then((r)=>process.exit(r.ok?0:1)).catch(()=>process.exit(1))"
CMD ["node", "dist/main.js"]
