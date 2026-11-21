ARG STITCH_BASE
FROM ${STITCH_BASE}
COPY --from=source . /src
WORKDIR /src
RUN npm ci --ignore-scripts --no-audit --no-fund && npm run build:node
RUN cd /opt && npm install --no-audit --no-fund /src
