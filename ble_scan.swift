// Lists nearby Bluetooth LE devices whose name contains "QF" or "Claude",
// using only Apple's CoreBluetooth. Run: swift ~/Downloads/ble_scan.swift
import CoreBluetooth
import Foundation

final class Scanner: NSObject, CBCentralManagerDelegate {
    var manager: CBCentralManager!
    var seen = Set<String>()
    var total = Set<UUID>()
    let nus = CBUUID(string: "6E400001-B5A3-F393-E0A9-E50E24DCCA9E")

    override init() {
        super.init()
        manager = CBCentralManager(delegate: self, queue: nil)
    }

    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        guard central.state == .poweredOn else {
            print("Bluetooth state: \(central.state.rawValue) (allow Terminal to use Bluetooth if asked)")
            return
        }
        let linked = central.retrieveConnectedPeripherals(withServices: [nus])
        for p in linked {
            print("Already connected to this Mac: name=\(p.name ?? "-") id=\(p.identifier)")
        }
        print("Scanning for 15 s...")
        central.scanForPeripherals(withServices: nil, options: [CBCentralManagerScanOptionAllowDuplicatesKey: false])
    }

    func centralManager(_ central: CBCentralManager, didDiscover p: CBPeripheral,
                        advertisementData: [String: Any], rssi RSSI: NSNumber) {
        let local = advertisementData[CBAdvertisementDataLocalNameKey] as? String ?? "-"
        let cached = p.name ?? "-"
        total.insert(p.identifier)
        guard (local + cached).contains("QF") || (local + cached).contains("Claude") else { return }
        let key = "\(p.identifier) \(local) \(cached)"
        if seen.contains(key) { return }
        seen.insert(key)
        let connectable = (advertisementData[CBAdvertisementDataIsConnectable] as? NSNumber)?.boolValue ?? false
        let services = (advertisementData[CBAdvertisementDataServiceUUIDsKey] as? [CBUUID]) ?? []
        print("Found: advertised name=\(local)  macOS name=\(cached)  connectable=\(connectable)  NUS=\(services.contains(nus))  rssi=\(RSSI)")
    }
}

let scanner = Scanner()
RunLoop.main.run(until: Date().addingTimeInterval(16))
print("Done. Bluetooth devices seen in total: \(scanner.total.count)")
