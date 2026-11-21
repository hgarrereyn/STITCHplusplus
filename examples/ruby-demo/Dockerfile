ARG STITCH_BASE
FROM ${STITCH_BASE}
COPY --from=source . /src
RUN cd /src && gem build kvrb.gemspec && gem install --local ./kvrb-*.gem
