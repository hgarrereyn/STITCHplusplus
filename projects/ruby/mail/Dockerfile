# The library from source: /src/lib first on the load path. mail 2.9 needs mini_mime; logger and
# net-smtp, net-imap and net-pop are Ruby's default and bundled gems.
ARG STITCH_BASE
FROM ${STITCH_BASE}
COPY --from=source . /src
RUN gem install --no-document mini_mime -v 1.1.5
ENV RUBYLIB=/src/lib
