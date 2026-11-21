// swift-tools-version:6.0
// The STITCH harness API for Swift. Linked with libstitch_swift_rt.a (the C/C++ runtime and the
// bridge) and, for fuzzing, libstitch_engine.a; see bin/stitch-build.
import PackageDescription

let package = Package(
    name: "Stitch",
    products: [.library(name: "Stitch", targets: ["Stitch"])],
    targets: [
        .target(name: "CStitch"),
        .target(name: "Stitch", dependencies: ["CStitch"], swiftSettings: [.swiftLanguageMode(.v5)]),
    ]
)
