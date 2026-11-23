
import Foundation
import CoreGraphics

@_silgen_name("SDVirtualDisplayCreate")
private func createVirtualDisplay(
    _ width: UInt32,
    _ height: UInt32,
    _ refreshRate: Double,
    _ displayID: UnsafeMutablePointer<UInt32>
) -> Int32

@_silgen_name("SDVirtualDisplayDestroy")
private func destroyVirtualDisplay()

final class VirtualDisplayManager {

    private(set) var displayID: CGDirectDisplayID?
    private var isCreated = false

    func create(
        width: Int,
        height: Int,
        fps: Int
    ) throws {

        guard !isCreated else {
            throw SDError.invalidState
        }

        guard width > 0, height > 0, fps > 0 else {
            throw SDError.invalidArgument
        }

        var createdID: UInt32 = 0

        let success = createVirtualDisplay(
            UInt32(width),
            UInt32(height),
            Double(fps),
            &createdID
        ) == 0

        guard success else {
            throw SDError.displayCreationFailed
        }

        displayID = CGDirectDisplayID(createdID)
        isCreated = true
    }

    func destroy() {
        guard isCreated else { return }

        destroyVirtualDisplay()

        displayID = nil
        isCreated = false
    }

    func getDisplayID() throws -> CGDirectDisplayID {
        guard let displayID else {
            throw SDError.invalidState
        }

        return displayID
    }
}
