# Installs the library under test where the harness finds it (/opt/node_modules, on NODE_PATH).
#   STITCH_BASE  Node.js 22 + the STITCH++ runtime
#   source       build context with the library source at the requested version
ARG STITCH_BASE
FROM ${STITCH_BASE}
COPY --from=source . /src
WORKDIR /src
# TypeScript or bundled packages: RUN npm install --ignore-scripts && npm run build
RUN cd /opt && npm install /src
