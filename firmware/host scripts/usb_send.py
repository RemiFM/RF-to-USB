import serial

# Configure the serial port
port = serial.Serial(
    port='COM5',        # Change to your port (e.g., COM3 on Windows)
    baudrate=115200,
    timeout=1
)

print("Connected to device. Type messages to send. Ctrl+C to exit.")

try:
    while True:
        user_input = input("> ")   # Get input from user
        port.write((user_input + '\n').encode())  # Send with newline


except KeyboardInterrupt:
    print("\nExiting...")

finally:
    port.close()