import asyncio
import streamlit as st
from bleak import BleakScanner, BleakClient
import datetime

st.title("One-Click BLE Time Sync")

SERVICE_UUID = "12345678-1234-5678-1234-56789abcdef0"
CHARACTERISTIC_UUID = "12345678-1234-5678-1234-56789abcdef1"

# --- CONFIGURATION ---
TARGET_DEVICE_NAME = "AS7341_ESP32_BLE"  # Replace with the exact name of your ESP32 BLE device
# --- END CONFIGURATION ---

timezones = {
    "UTC": "UTC0",
    "CET (Central Europe)": "CET-1CEST,M3.5.0/2,M10.5.0/3",
    "EST (Eastern US)": "EST5EDT,M3.2.0,M11.1.0",
    "PST (Pacific US)": "PST8PDT,M3.2.0,M11.1.0"
}
tz_choice = st.selectbox("Select Timezone", list(timezones.keys()), index=0)
tz_string = timezones[tz_choice]

async def scan_for_target_device():
    """Scans for a specific BLE device by name and returns its address."""
    scanner = BleakScanner()
    st.info(f"Scanning for device: '{TARGET_DEVICE_NAME}'...") # Less intrusive info message
    await scanner.start()
    await asyncio.sleep(5) # Short scan duration, adjust if needed
    await scanner.stop()

    target_device = None
    for d in scanner.discovered_devices:
        if d.name == TARGET_DEVICE_NAME:
            target_device = d
            break

    if target_device:
        st.success(f"Device '{TARGET_DEVICE_NAME}' found: {target_device.address}")
        return target_device.address
    else:
        st.warning(f"Device '{TARGET_DEVICE_NAME}' not found. Please ensure it is advertising.")
        return None

async def send_time_to_esp32(ble_address):
    """Connects to the ESP32 and sends the current timestamp."""
    if not ble_address:
        st.error("No device address provided. Scan failed or device not found.")
        return

    try:
        st.info(f"Connecting to {ble_address}...") # Less intrusive info message
        async with BleakClient(ble_address) as client:
            if not client.is_connected:
                st.error(f"Failed to connect to {ble_address}")
                return
            st.success(f"Connected to {TARGET_DEVICE_NAME} ({ble_address})")

            # Prepare the time string
            now = datetime.datetime.now()
            time_str = now.strftime("%Y-%m-%d %H:%M:%S")
            message = f"{time_str}|{tz_string}"
            st.write(f"Sending time: {message}")

            # Write to characteristic
            await client.write_gatt_char(CHARACTERISTIC_UUID, message.encode())
            st.success("Time successfully sent to device!")
    except Exception as e:
        st.error(f"Error: {e}")


if st.button("Sync Time with Device"):
    device_address = asyncio.run(scan_for_target_device())
    if device_address:
        asyncio.run(send_time_to_esp32(device_address))