// swift-tools-version:5.9
import PackageDescription

let package = Package(
    name: "PfsynthDemo",
    platforms: [.macOS(.v14)],
    targets: [
        // The synthesis kernel and player: symlinks to the repository sources, compiled as one C module.
        .target(
            name: "PfsynthCore",
            path: "Sources/PfsynthCore",
            publicHeadersPath: "include",
            cSettings: [.unsafeFlags(["-O2", "-std=c99"])]
        ),
        .executableTarget(
            name: "PfsynthDemo",
            dependencies: ["PfsynthCore"],
            path: "Sources/PfsynthDemo",
            linkerSettings: [.linkedFramework("AVFoundation"), .linkedFramework("CoreMedia"), .linkedFramework("CoreVideo"), .linkedFramework("AppKit")]
        )
    ],
    swiftLanguageVersions: [.v5]
)
