# Quote Win Native

Have you ever seen your Windows boot screen display text like this?

<img width="651" height="282" alt="image" src="https://github.com/user-attachments/assets/5c5eda1d-d43e-4514-9f73-702577fead16" />

Do you want to display text too on your boot screen using your preference?

Well, this apps might be answer for your question :).

## Introduction
*Quote Win Native* is an application that display text similar what you see on some game loading screen.

Here's my own laptop displaying custom text "Hello, world!" similar with what you see when there's disk checking running on boot time.

Quote Win Native works by reading file on `%SystemRoot%\Windows\System32` or in many case it's located on `C:\Windows\System32\`.

It reads `quotes.txt` from there with UTF-16LE encoding since Windows internally use UCS-2.

Each character delay 50 ms and 150 ms if it's punctional character.

Between line there's a 2 sec delay time.

In case you're in a hurry and you want to skip, you can press any button on (internal on laptop) keyboard.

## File format
Comment is started with \# and the entire line will be ignored.

Between quotes is separed with triple strip (---).

If the quotes contains more than one line, just create newline and continue the quotes.

*MAKE SURE YOU'RE USING UTF-16LE ENCODING*

<img width="480" height="318" alt="image" src="https://github.com/user-attachments/assets/8f85ae38-cb1f-4108-a500-6bc5e7807cc9" />

I provided the example file so you can just edit with your own preferences.

## How this work?

<img width="963" height="1280" alt="image" src="https://github.com/user-attachments/assets/72d2c989-c14d-4b40-a3e0-c0b8423b329c" />


Read [this](https://medium.com/windows-os-internals/windows-native-api-programming-hello-world-8f256abe1c85?sharedUserId=kawaiighost), you can just skip into the Native Subsystem parts.

## What's the rationale of using file mapping instead of reading file and store it into buffer?
- I don't want to handle the buffer manually.
- `NtReadFile` can't show the file size while you can use `ViewSize` parameter `NtMapViewOfSection` to get the file size.
- Copy-on-write yeah!

# Install & Uninstall

Just run install.ps1 as Admin :).
To uninstall, run uninstall.ps1 too.


