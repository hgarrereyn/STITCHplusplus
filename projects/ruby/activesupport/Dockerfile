# ActiveSupport from the rails checkout, on the load path. Its gem dependencies, plus the gems
# Ruby 3.4 no longer ships as default gems (bigdecimal, base64, mutex_m, drb) and builder (to_xml).
ARG STITCH_BASE
FROM ${STITCH_BASE}
COPY --from=source . /src
RUN gem install --no-document i18n -v 1.14.7 && gem install --no-document tzinfo -v 2.0.6 && gem install --no-document concurrent-ruby -v 1.3.4 \
    && gem install --no-document minitest -v 5.25.4 && gem install --no-document bigdecimal base64 mutex_m drb builder logger
ENV RUBYLIB=/src/activesupport/lib
