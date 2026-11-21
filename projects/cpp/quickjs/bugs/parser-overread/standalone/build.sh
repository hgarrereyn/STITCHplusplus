set -e
clang++ -std=c++17 $(stitch-flags --no-cov) -I. -I/src repro.cpp /src/build/libqjs.a -lm -ldl -lpthread -lstdc++ -lm -lpthread -o /tmp/repro
ASAN_OPTIONS=abort_on_error=1:symbolize=1:allocator_may_return_null=1:handle_abort=1:detect_stack_use_after_return=0:malloc_context_size=10:detect_leaks=0 /tmp/repro
