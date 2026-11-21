# handlebars' parser is generated at release time (jison), so the image installs the published
# package of the version in /src/package.json (follows `stitch build --rev`).
ARG STITCH_BASE
FROM ${STITCH_BASE}
COPY --from=source . /src
RUN cd /opt && npm install --ignore-scripts --no-audit --no-fund "handlebars@$(node -p "require('/src/package.json').version")"
