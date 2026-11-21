# mathjs is built (babel/gulp) at release time: the image installs the published package of the
# version in /src/package.json (follows `stitch build --rev`).
ARG STITCH_BASE
FROM ${STITCH_BASE}
COPY --from=source . /src
RUN cd /opt && npm install --ignore-scripts --no-audit --no-fund "mathjs@$(node -p "require('/src/package.json').version")"
