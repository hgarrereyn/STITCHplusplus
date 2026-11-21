# The library from source: /src/lib first on the load path. mail 2.7 needs mini_mime; net-smtp &
# co. are Ruby's bundled gems.
ARG STITCH_BASE
FROM ${STITCH_BASE}
COPY --from=source . /src
RUN gem install --no-document mini_mime -v 1.1.5
ENV RUBYLIB=/src/lib
