# DivoomClient for ESP32
Decrypt Divoom's animations so they could be displayed on LCD screen, LED matrix...

# Preface
I'm really really new to Arduino/C++, so if you see I wrote something stupid, feel free to correct. I really appreciate it.  
The library is still in early phase, but I can't continue working on it at the moment because the internet connection in where I live is extremely slow (my M5StickC couldn't even connect to Divoom server).

# Demo
![Demo](https://user-images.githubusercontent.com/96280/216748757-42c2a993-e91c-4c64-8567-b1f133772bcc.gif)  
[DivoomClient on M5StickC Plus (ESP32)](https://youtube.com/watch/gyZQLm-OVRY)

# How does it work?
1. Download encrypted animation from Divoom server.
2. Decrypt using AES & LZO, then translate it to understandable frames data.
3. Render frames.

# How to run the example
This example I made for M5StickC Plus. May or may not work on another ESP32 devices.

1. Add DivoomClient library to Arduino.
2. Download Divoom app on Android/iOS and create an account.
3. Get MD5 hash of your password. You could use [JavaScript-MD5](https://blueimp.github.io/JavaScript-MD5/).
4. Update values in `configs.h`.
5. Update `renderFrames()` if you don't use LCD screen. It should be easy to modify it for LED matrix.
5. Compile.

# TODO
1. Fix random crashing/memory problem. I'm not good at managing memory yet, so please help me on this if you can.
2. Add support for 32x32 and 64x64 animations. Actually I already had the working code for this, but I need to make sure the library work properly on 16x16 animations first.

# Acknowledgements
- [khoih-prog/AsyncTCP_SSL](https://github.com/khoih-prog/AsyncTCP_SSL)
- [rweather/arduinolibs](https://github.com/rweather/arduinolibs)
- [bblanchon/ArduinoJson](https://github.com/bblanchon/ArduinoJson)
- [lovyan03/LovyanGFX](https://github.com/lovyan03/LovyanGFX)
- [minilzo](http://www.oberhumer.com/opensource/lzo/)
