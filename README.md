# BMW F30 REMOTE START
BMW F30 Remote start feature with Arduino

# How it works
The Arduino, thanks to the MCP2515 board, constantly listen to the BMW K-CAN2 bus, looking for a triple lock button click. If found, it starts a 18 seconds long operation, during which it does those steps:
- Turns on the in-car keyfob
- Unlocks the car
- Locks the car 
- Virtually presses the start button, turning on the ignition
- Waits some seconds, to let the engine be ready to start.
- Virtually presses the brake and the start button, turning on the engine
- Turns off the in-car key.

# Security issues
**The spare key is always in the car. Wouldn't this be enough to allow a thief to turn on the engine?**
Yes, but actually no. The Arduino turns on the keyfob only when it needs it to remote start. Normally the key isn't powered so the car doesn't see it and can't be turned on!

**Why do the Arduino open and close the car before starting the engine?**
The lock and unlock thing is needed to be able to turn on the ignition. This is because of the BMW security protocol, which disables the possibility of turning on the car if the car wasn't opened before. This is not a security issues though, because this process happens in 2 seconds and the car remains open for just 1 second.

# Required things
Here's a list of boards you need for this project:
- Arduino Nano
- LM2596S voltage regulator board
- MCP2515 Canbus board (Niren)
- 6x Relay Board 5V

# Car Wiring
Here you can see how to wire break light sensor, start/stop button, canbus and power.
- The start-stop button wiring is right behind the start button, you just have to remove it and hook to its wires.
- The brake light switch wiring can be found over the brake pedal.
- The KCAN2 wiring can be found at the FEM. Attention: the OBD2 port doesn't have KCAN2. You can't use it.
- The 12V/GND needs to be always powered. You can find it at the FEM. 

![CAR Wiring](images/wiring_car_1.png)
![CAR Wiring](images/wiring_car_2.png)
![CAR Wiring](images/wiring_car_3.png)

# KeyFob Wiring
Here you can see how to hook to spare keyfob power and unlock and lock buttons
- You have to interrupt the battery positive connection, split it in 2 wires and feed them inside the relay.

![KEY_POWER wiring](images/wiring_keyfob_1.jpg)

![KEY_LOCK/UNLOCK wiring](images/wiring_keyfob_2.jpg)
