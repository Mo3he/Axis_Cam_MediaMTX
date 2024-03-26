# The MediaMTX installer ACAP

This ACAP packages the scripts and files required to install the MediaMTX server on Axis Cameras.

Current version 1.6.0


## Warning
Unfortunately Axis is making changes to its firmware that will prevent the use of a lot of ACAPs including my own and as of today there is no way to ready my ACAPs for these changes.
 
You can read more here
 
https://help.axis.com/en-us/axis-os#upcoming-breaking-changes

If you have a use case where certain functionality used by an ACAP application currently requires root-user permissions or have a question about ACAP application signing, please contact Axis at acap-privileges@axis.com

Thank you for your continued support.

## Purpose

MediaMTX (formerly rtsp-simple-server) is a ready-to-use and zero-dependency real-time media server and media proxy that allows to publish, read, proxy, record and playback video and audio streams. It has been conceived as a "media router" that routes media streams from one end to the other.

## Links

https://github.com/bluenviron/mediamtx

https://www.axis.com/

## Compatibility

The Axis_Cam_MediaMTX is compatable with Axis cameras with arm and aarch64 based Soc's.

```
curl --anyauth "*" -u <username>:<password> <device ip>/axis-cgi/basicdeviceinfo.cgi --data "{\"apiVersion\":\"1.0\",\"context\":\"Client defined request ID\",\"method\":\"getAllProperties\"}"
```

where `<device ip>` is the IP address of the Axis device, `<username>` is the root username and `<password>` is the root password. Please
note that you need to enclose your password with quotes (`'`) if it contains special characters.

## Installing

The recommended way to install this ACAP is to use the pre built eap file.
Go to "Apps" on the camera and click "Add app".


## Using the MediaMTX ACAP

You will need to upload your own mediamtx.yml config file via sftp otherwise a blank config will be used.

An exapmple is included and you need to place it in /usr/local/packages/MediaMTX

https://github.com/Mo3he/Axis_Cam_MediaMTX/blob/main/mediamtx.yml

https://github.com/bluenviron/mediamtx?tab=readme-ov-file#configuration

For example

Allow anonymous viewing of RTSP stream

Adding 
```
paths:
      proxied:
          source: rtsp://user:password@IPAddress/axis-media/media.amp?videocodec=h264&resolution=640x480
```
at the end of the mediamtx.yml will make the stream available at rtsp://IPAddress:8554/proxied with no authentication.

The MediaMTX ACAP will run a script on startup that sets the required permissions and starts the service and app.
Once started click "Open" to see the output of the logs.

Further commands can then be issues via ssh.

When uninstalling the ACAP, all changes and files are removed from the camera.


## Build from source
To build, 
From main directory of the version you want (arm/aarch64)

```
docker build --tag <package name> . 
```
```
docker cp $(docker create <package name>):/opt/app ./build 
```





