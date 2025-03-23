# `wprouter` - A simple C++ Websocket router for Linux

This lightweight Websockets router facilitates Websocket communication between IoT devices, in a star network architecture, with the physical network router in the center. In other words, if you have multiple IoT devices to talk to each other, this router may be the solution to organize data traffic. I wrote this program for a **GL.iNet GL-MT300N-V2** pocket router (an awesome piece of hardware), but it should work on basically anything, including microcontrollers.

## How to build it

You will need `websocketpp`, `asio` and the appropriate C++ build tool for your target platform. The `build.sh` script is there for your convenience, with some instructions on what to download and where to find them. A binary build is also included, for the `mipsel` architecture.

## How to use it

The easiest way to run `wprouter` is simply to log in to your device with SSH and run it. You can turn it into a service to run in the background - see later.

Any Websocket client within the network may connect to the router's IP address, and send messages to any other client through the router.

Each client must have a unique alphanumeric ID of your choice, such as `llm`, `database`, `camera`, etc. The names may only contain letters and numbers. The name `router` is reserved for the router itself. If there are two clients with the same ID, the one connecting later will disconnect the older one.

After connection, a client must send a `hello` message to the router to identify itself. More about it later.

## General message format

```
<recipient>::<sender>::<reply expected>::<reply to>::<content>
```

`recipient` is the ID of the client to receive the message. A `*` means a message to every connected client.
`sender` is the ID of the sender client.
`expects reply` is `1` or `0`, and indicates whether the sender expects a response.
`reply to` is the ID of the client that should receive the response. It can be the same as the sender, but doesn’t have to be. Note that this isn't validated, and can be anything, even a nonexistent client ID. It's the recipient's job to sort it out.
`content` is the payload of the message. It may contain any character, including Unicode and emojis.

For example, in a system of three devices where `whisper` is used for text recognition, `llama` as an LLM and `bark` as a text-to-speech engine, `whisper` could send this to the router after receiving a voice prompt from the user. The message instructs the router to forward the message to `llama`, which is then instructed to send its response (output) to `bark`. Thus, a talking robot is created from 3 separate units.

```
llama::whisper::1::bark::Hello world!
```

The router will chop off the name of the recipient before forwarding, so `llama` will receive:

```
whisper::1::bark::Hello world!
```

## Commands to the router

The router accepts a few commands from clients. These messages have a different format from others:

```
router::<sender>::<command>::<argument 1>::<argument 2>::<argument 3>::...
```

For example:

```
router::whisper::hello::whisper::
```

Valid commands and their parameters (in brackets) are the following:

### `hello`
- Identifies the client after connection. The first parameter is the id of the client, which is redundancy, just get over it
- A client cannot receive any messages before "introducing itself". 
- However, it can already send messages. The router will automatically register it, so `hello` is somewhat optional.
- Unconfirmed connections will stay connected and reserve a connection slot, so be sure to either introduce or disconnect them.

### `ping`
A ping. The router responds to the sender with a system message:

```
router::0::::pong
```

### `disconnect::<client id>`
- Instructs the router to disconnect a certain client. 
- An empty `client_id` will disconnect every unconfirmed client. 
- Sending `*` will disconnect every client, confirmed or unconfirmed, including the sender.
- If a client disconnects itself, either by using `*` or directly specifying its own ID, then the router sends no response. 
- Otherwise the response is:

```
router::0::::Client <client id> disconnected.
```

### `clients::<client_id>`
- Returns the client ID if the specified client is connected. If it doesn't exist, an error message is returned.
- If the ID is `*`, it returns a list of all connected client IDs as a comma-separated list. 
- If the ID is empty, the response will be the number of confirmed and unconfirmed clients.
- Examples:

```
//  ID specified
router::0::::llama

//  *
router::0::::whisper,llama,bark

//  No ID specified
router::0::::3,0
```

### `version`
Returns the build date and version of the router.

## Errors

If there is an error in what the router received, the response is given to the sender client in the following form:

```
router::<error code>::::<error message>
```

When a message is received from the `router`, it never needs a reply, so the `reply to` flag is repurposed: `0` means a system message, anything higher is an error code.

Possible error messages:

|Code|Message|Reason|
|---|---|---|
|1|`Message could not be parsed`|You sent something garbled|
|2|`Message is incomplete`|The message received by the router had less than 5 fields|
|3|`Client "<recipient>" is not connected to server`|You're attempting to message a nonexistent client|
|4|`Invalid <sender/recipient/reply> ID: \"<id>\"`|A non-alphanumeric character was found in one of the client IDs (sender or recipient)|
|5|`<Sender/Recipient> not specified`|Either the sender or the recipient ID was missing|
|6|`The router cannot be marked as sender, or be replied to.`|You specified `router` as sender or reply ID|
|7|`Router is full`|Maximum number of connections reached, the client can't connect to the router|
|8|`Invalid command: "<command>"`|The command wasn't recognized by the router|

### What will NOT cause an error:

- More than 5 fields
- The payload contains `::`
- An unconfirmed client trying to send a message (before sending `hello`)

## Logging

Run the program with `--log` or `-l` command line argument to see message traffic on the console:

```
2025-04-11 12:02:20 [RECV] database::frontend::0::llm::Hello world!
```

|Flag|Meaning|
|---|---|
|`[RECV]`|Received|
|`[SENT]`|Sent|
|`[ERROR]`|Error message sent|
|`[SYSTEM]`|System message sent|

##  Command line arguments

|Argument|Shorthand|Meaning|Default value|
|---|---|---|---|
|--port|-p|Port number|8080|
|--connections|-c|Maximum number of Websocket clients|10|
|--log|-l|Console logging on||
|--version|-v|Show version number||
|--help|-h|This help||

# Installing as a Linux service

To run `wprouter` as a Linux service automatically upon boot:

1. Copy the binary to `/usr/bin` on your device.
2. Create a new file: `/etc/init.d/wprouter`
3. Paste this into the file:

```
#!/bin/sh /etc/rc.common

START=99
STOP=10

USE_PROCD=1

start_service() {
    procd_open_instance
    procd_set_param command /usr/bin/wprouter
    procd_set_param respawn
    procd_close_instance
}
```
4. Mark it as executable:

```
chmod +x etc/init.d/wprouter
```

5. Create a symlink:

```
ln -s /etc/init.d/wprouter etc/rc.d/S99wprouter
```

(Be mindful of the relative paths, don't add `/` before the symlink path! That would create a symlink in your own system.)

While running `wprouter` as a service, you can monitor its log output with:

```
logread | grep wprouter
```

or

```
logread | grep wprouter | tail
```

# Integrating `wprouter` into your firmware

You may find that your device's storage space is sized precisely for the firmware, and there isn't any extra room. In this case, you need to alter the firmware to include `wprouter`.

1. Download the firmware as a binary image. Name it `firmware.bin` for the sake of simplicity.
2. Look into the file with a hexadecimal viewer (such as Midnight Commander) and find a signature: `hsqs` or `squashfs`.
3. Note the position where it begins, and convert it to decimal.
4. Extract the `rootfs` image using `dd`:

```
dd if=firmware.img of=rootfs.squashfs bs=1 skip=xxxxxxx
```

Replace `xxxxxxx` with the decimal position value. This will create a `rootfs.squashfs` file in the same directory.

5. Unpack this file:
```
unsquashfs rootfs.squashfs
```

If the correct position was supplied, this will create a `squashfs-root/` subdirectory with a Linux directory structure in it. This is the firmware.

6. Add `wprouter` to this Linux structure the same way as described in the previous section.
7. Repack the modified firmware:
```
mksquashfs squashfs-root new-rootfs.squashfs -comp xz -noappend
```
8. Create a new firmware image from the old one, and overwrite its contents with what you just created:
```
cp firmware.bin new_firmware.bin
dd if=new-rootfs.squashfs of=new_firmware.bin bs=1 seek=1507328 conv=notrunc
```
9. Use the newly created `new_firmware.bin` to upgrade your device firmware.

10. If the device refuses to accept the binary because checksums don't match, you can (probably) circumvent it by SSHing into the device, mounting an USB device with the image, and using this command:

```
sysupgrade -n -F /mnt/usb/new_firmware.bin
```

If you upgrade the firmware in the future, you will need to repeat these steps.

Yes, I'm aware that there are alternative ways to do this, and *sigh* yes, your method is better than mine.

# Some remarks

- Only `ws://` is supported, not `wss://`.
- This is a very simple router. It's not a good idea to use it in a secure environment.
- Feel free making a push request if you want to improve this thing!
