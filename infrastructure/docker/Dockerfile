# syntax=docker/dockerfile:1
#
# AKENO NETWORK production image: one Node.js process serving the API, the WebSocket gateway
# and the built web client from the same origin. Build from the repository root:
#   docker build -f infrastructure/docker/Dockerfile -t akeno-network .
#
# Behind a TLS-intercepting proxy, pass its CA certificate as a build secret (it is only
# mounted while installing packages and never stored in an image layer):
#   docker build --secret id=extra_ca,src=/path/to/ca.crt -f infrastructure/docker/Dockerfile .

ARG NODE_IMAGE=node:22-alpine

FROM ${NODE_IMAGE} AS build
ENV CI=true
RUN --mount=type=secret,id=extra_ca,required=false \
    if [ -s /run/secrets/extra_ca ]; then export NODE_EXTRA_CA_CERTS=/run/secrets/extra_ca; fi; \
    npm install --global pnpm@10.28.0
WORKDIR /repo

# Install dependencies first so this layer is cached until a manifest changes.
COPY package.json pnpm-lock.yaml pnpm-workspace.yaml ./
COPY apps/server/package.json apps/server/
COPY apps/web/package.json apps/web/
COPY packages/shared/package.json packages/shared/
RUN --mount=type=secret,id=extra_ca,required=false \
    if [ -s /run/secrets/extra_ca ]; then export NODE_EXTRA_CA_CERTS=/run/secrets/extra_ca; fi; \
    pnpm install --frozen-lockfile

COPY tsconfig.base.json ./
COPY packages/shared packages/shared
COPY apps/server apps/server
COPY apps/web apps/web
RUN pnpm --filter @akeno/server run build \
 && pnpm --filter @akeno/web exec vite build
# Assemble the server's production dependencies (pnpm may fetch packages here too).
RUN --mount=type=secret,id=extra_ca,required=false \
    if [ -s /run/secrets/extra_ca ]; then export NODE_EXTRA_CA_CERTS=/run/secrets/extra_ca; fi; \
    pnpm --filter @akeno/server deploy --prod --legacy /deploy

FROM ${NODE_IMAGE} AS runtime
ENV NODE_ENV=production \
    HOST=0.0.0.0 \
    PORT=3000 \
    WEB_DIST_DIR=/app/web \
    MIGRATIONS_DIR=/app/dist/migrations \
    MIGRATE_ON_START=true
WORKDIR /app
COPY --from=build --chown=node:node /deploy/package.json ./package.json
COPY --from=build --chown=node:node /deploy/node_modules ./node_modules
COPY --from=build --chown=node:node /repo/apps/server/dist ./dist
COPY --from=build --chown=node:node /repo/apps/web/dist ./web

# Never run the application as root.
USER node
EXPOSE 3000
HEALTHCHECK --interval=30s --timeout=5s --start-period=40s --retries=3 \
  CMD node -e "fetch('http://127.0.0.1:3000/health/ready').then((r)=>process.exit(r.ok?0:1)).catch(()=>process.exit(1))"
CMD ["node", "dist/main.js"]
