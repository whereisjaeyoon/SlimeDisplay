import Foundation

struct SDConfiguration {
    let width: Int
    let height: Int
    let fps: Int

    var isValid: Bool {
        width >= 320 &&
        height >= 240 &&
        width <= 7680 &&
        height <= 4320 &&
        fps > 0 &&
        fps <= 120
    }
}
