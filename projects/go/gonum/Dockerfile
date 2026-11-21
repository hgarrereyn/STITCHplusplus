# Puts the library source at /src and downloads its modules.
#   STITCH_BASE  Go + the STITCH++ runtime
#   source       build context with the library source at the requested version
ARG STITCH_BASE
FROM ${STITCH_BASE}
COPY --from=source . /src
RUN cd /src && go mod download
# gonum >= 0.15 takes math/rand/v2 sources instead of golang.org/x/exp/rand ones. The harness is
# written against x/exp/rand; for those versions, a shim module of that name aliases math/rand/v2
# (picked up by stitch-build from /etc/stitch/go-replace), so the same harness builds at every release.
RUN if ! grep -q "golang.org/x/exp" /src/go.mod; then \
      mkdir -p /opt/xexp-shim/rand /etc/stitch && \
      printf 'module golang.org/x/exp\n\ngo 1.22\n' > /opt/xexp-shim/go.mod && \
      printf 'package rand\n\nimport r2 "math/rand/v2"\n\ntype Source = r2.Source\ntype Rand = r2.Rand\n\nfunc NewSource(seed uint64) Source { return r2.NewPCG(seed, seed^0x9e3779b97f4a7c15) }\nfunc New(src Source) *Rand { return r2.New(src) }\n' > /opt/xexp-shim/rand/rand.go && \
      echo "golang.org/x/exp => /opt/xexp-shim" > /etc/stitch/go-replace; \
    fi
