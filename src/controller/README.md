# Gamepad controller adapters

`gameboy::Controller` owns NimBLE only while gameplay is active. The setup BLE
service must be stopped before `start()`. `stop()` requests shutdown and waits
up to three seconds; if it returns false, the caller must keep the setup radio
off and retry until `active()` is false. The worker alone completes scan/GATT
operations, disconnects the client, and calls `NimBLEDevice::deinit(true)`.
There is no `esp_bt_mem_release`: a later setup service may use NimBLE again.
`start()` refuses a controller already owned by another BLE service; if NimBLE
initialization fails after claiming an idle controller, it unwinds that partial
claim before returning.

`start()` defaults to Bluetooth. `start(Controller::Transport::UsbSerial)` uses
the ESP32-S3 native USB port as a host for the ESP32-C3 gamepad's native
USB Serial/JTAG device (VID `303A`, PID `1001`). This transport does not start
NimBLE. The host accepts only its CDC control interface zero and data interface
one, opens DTR with RTS low, and reads the bulk IN endpoint. RTS stays low to
avoid a serial-control reset of the C3. The serial console on the S3 is ended
before USB host installation. On stop or initialization failure, the host
closes the device, drains transfers, releases the stack and OTG PHY, routes the
internal PHY back to Serial/JTAG, and restarts the console. USB-host startup
must complete before `start()` returns true; a missing gamepad leaves the host
ready but `connected()` false. The caller should retry `stop()` if its bounded
wait returns false. If a control request is pending during shutdown, the host
powers off its root port to make IDF complete the request, then waits for the
completion callback before freeing the transfer. A control request that has
not completed after one second is treated as failed; the port is power cycled
after cleanup so an attached controller can enumerate again.

USB reports use `GP1:SSSS:MM:CC\n` (15 ASCII bytes), with a 16-bit sequence,
eight-bit HID button mask, and CRC-8 polynomial `0x07` over protocol version,
sequence, and mask. The host discards malformed, repeated, and older frames.
Disconnect releases buttons immediately; a 2-second report timeout also
releases them when a cable or producer stalls. Button mapping and its
`gamepad/buttons` NVS storage are shared with Bluetooth mode.

This adapter supports the **tested ESP32 Gamepad firmware**, not arbitrary HID
controllers. It matches the advertised name `ESP32 Gamepad`, the HID service
`0x1812`, a notifying Report characteristic `0x2A4D` whose Report Reference
descriptor `0x2908` is `[3, 1]` (input report 3). The verified unit sent a
19-byte notification; this adapter accepts 2–32 bytes: at least the two
button bytes, with an upper bound on the report size.
NimBLE gives the callback report data without the report ID. Byte zero is the
button bitmap; the controller currently uses buttons 1–8. Its 250 ms heartbeat
allows the adapter to release stale input after two seconds, including while a
button is held. A disconnect releases all buttons and restarts discovery.

The saved mapping is the `gamepad/buttons` eight-byte NVS blob, indexed in Game Boy order A, B, Select, Start, Right, Left, Up, Down.
Only a permutation of the eight single-bit masks is accepted. If no valid
mapping exists, the adapter uses the InkBoy controller wiring:
`40 20 10 80 08 02 04 01` (hex). `setMapping()` can replace and persist it.
NVS initialization errors never erase stored data.

Host-side controller checks, including USB frame/descriptor and console
lifecycle tests: `./check-host.sh`.
