// Fuzzer driver: hands control to the LibAFL engine (libstitch_engine.a).
extern "C" int stitch_engine_main(int argc, char** argv);
int main(int argc, char** argv) { return stitch_engine_main(argc, argv); }
