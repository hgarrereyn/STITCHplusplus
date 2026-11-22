# Installs the library under test where the harness finds it (/opt/node_modules, on NODE_PATH).
#   STITCH_BASE  Node.js 22 + the STITCH++ runtime
#   source       build context with the library source at the requested version
ARG STITCH_BASE
FROM ${STITCH_BASE}
COPY --from=source . /src
WORKDIR /src
# TypeScript or bundled packages: RUN npm install --ignore-scripts && npm run build
# marked >= v5 does not commit lib/: build it
RUN if [ ! -f lib/marked.cjs ]; then npm install --ignore-scripts --no-audit --no-fund && npm run build; fi
# ESM-only package: give node a CommonJS copy of the UMD build for require()
RUN if [ -f lib/marked.umd.js ] && [ ! -f lib/marked.cjs ]; then cp lib/marked.umd.js lib/marked.cjs; fi
RUN cd /opt && npm install /src
