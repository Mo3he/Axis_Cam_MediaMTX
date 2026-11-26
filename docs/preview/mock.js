// Sample data for the public, offline preview of the MediaMTX web UI. Nothing here
// talks to a camera: config.cgi is answered in the browser with made-up data.
// The other files in this folder are copies of app/html; refresh them by
// re-copying and re-adding <script src="mock.js"></script> as the first script
// in <head>. DEFAULTS_YML below is a verbatim copy of app/mediamtx.defaults.yml.
(function () {
  'use strict';

  const DEFAULTS_YML = String.raw`###############################################
# Global settings

# Settings in this section are applied anywhere.

###############################################
# Global settings -> General

# Verbosity of the program; available values are "error", "warn", "info", "debug".
logLevel: info
# Destinations of log messages; available values are "stdout", "file" and "syslog".
logDestinations: [stdout]
# When destination is "stdout" or "file", emit logs in structured format (JSONL).
logStructured: false
# When "file" is in logDestinations, this is the file which will receive logs.
logFile: mediamtx.log
# When "syslog" is in logDestinations, use prefix for logs.
sysLogPrefix: mediamtx
# Dump packets to disk. This is useful for debugging.
dumpPackets: false

# Timeout of read operations.
readTimeout: 10s
# Timeout of write operations.
writeTimeout: 10s
# Size of the queue of outgoing packets.
# A higher value allows to increase throughput, a lower value allows to save RAM.
writeQueueSize: 512
# Maximum size of outgoing UDP payloads.
# It defaults to the maximum packet size on ethernet (1500) minus IPv6 and UDP headers (48).
# This can be decreased to avoid fragmentation on networks with a low MTU.
udpMaxPayloadSize: 1452
# Size of the read buffer of every UDP socket.
# This can be increased to decrease packet losses.
# It defaults to the default value of the operating system.
udpReadBufferSize: 0

# Command to run when a client connects to the server.
# This is terminated with SIGINT when a client disconnects from the server.
# The following environment variables are available:
# * MTX_CONN_TYPE: connection type
# * MTX_CONN_ID: connection ID
# * RTSP_PORT: RTSP server port
runOnConnect:
# Restart the command if it exits.
runOnConnectRestart: false
# Command to run when a client disconnects from the server.
# Environment variables are the same as runOnConnect.
runOnDisconnect:

###############################################
# Global settings -> Authentication

# Authentication method. Available values:
# * Internal database: credentials are stored in the configuration file
# * External HTTP server: an external HTTP URL is contacted for each authentication request
# * External JWT provider: credentials are signed tokens released by an external identity server
authMethod: internal

# Internal authentication.
# Enabled users.
authInternalUsers:
  # Default unprivileged user.
  # Username. 'any' means any user, including anonymous ones.
  - user: any
    # Password. Not used in case of 'any' user.
    pass:
    # IPs or networks allowed to use this user. An empty list means any IP.
    ips: []
    # Permissions.
    permissions:
      # Available actions are: publish, read, playback, api, metrics, pprof.
      - action: publish
        # Paths can be set to further restrict access to a specific path.
        # An empty path means any path.
        # Regular expressions can be used by using a tilde as prefix.
        path:
      - action: read
        path:
      - action: playback
        path:

    # Default administrator.
    # This allows to use API, metrics and PPROF without authentication,
    # if the IP is localhost.
  - user: any
    pass:
    ips: ["127.0.0.1", "::1"]
    permissions:
      - action: api
      - action: metrics
      - action: pprof

# HTTP-based authentication.
# URL called to perform authentication. Every time a user wants
# to perform an action, the server calls this URL with the POST method
# and a payload described in the documentation.
# If the response code is 20x, the action is allowed, otherwise
# it is forbidden.
authHTTPAddress:
# If the HTTP authentication URL has a self-signed or invalid certificate,
# you can provide the fingerprint of the certificate in order to
# validate it anyway. It can be obtained by running:
# openssl s_client -connect auth_http_domain:443 </dev/null 2>/dev/null | sed -n '/BEGIN/,/END/p' > server.crt
# openssl x509 -in server.crt -noout -fingerprint -sha256 | cut -d "=" -f2 | tr -d ':'
authHTTPFingerprint:
# Actions to exclude from HTTP-based authentication.
# Format is the same as the one of user permissions.
authHTTPExclude:
  - action: api
  - action: metrics
  - action: pprof

# JWT-based authentication.
# Users have to log in through an external identity server and obtain a JWT.
# This JWT must contain the claim "mediamtx_permissions" with permissions,
# for instance:
# {
#  "mediamtx_permissions": [
#     {
#       "action": "publish",
#       "path": "somepath"
#     }
#   ]
# }
# Users are expected to pass the JWT in the Authorization header or as password.
# This is the JWKS URL that will be used to pull (once) the public key that allows
# to validate JWTs.
authJWTJWKS:
# If the JWKS URL has a self-signed or invalid certificate,
# you can provide the fingerprint of the certificate in order to
# validate it anyway. It can be obtained by running:
# openssl s_client -connect jwt_jwks_domain:443 </dev/null 2>/dev/null | sed -n '/BEGIN/,/END/p' > server.crt
# openssl x509 -in server.crt -noout -fingerprint -sha256 | cut -d "=" -f2 | tr -d ':'
authJWTJWKSFingerprint:
# name of the claim that contains permissions.
authJWTClaimKey: mediamtx_permissions
# Actions to exclude from JWT-based authentication.
# Format is the same as the one of user permissions.
authJWTExclude: []
# Expected issuer (iss) claim in the JWT. Leave empty to skip validation.
authJWTIssuer:
# Expected audience (aud) claim in the JWT. Leave empty to skip validation.
authJWTAudience:

###############################################
# Global settings -> Control API

# Enable the control API server, which allows to control the server.
# Required: the settings and Recordings pages read server status through it.
api: true
# Address of the TCP/HTTP listener.
apiAddress: :9997
# Enable HTTPS.
apiEncryption: false
# Path to the server key. This is needed only when encryption is yes.
# This can be generated with:
# openssl genrsa -out server.key 2048
# openssl req -new -x509 -sha256 -key server.key -out server.crt -days 3650
apiServerKey: server.key
# Path to the server certificate.
apiServerCert: server.crt
# Allowed CORS origins.
# Supports wildcards: ['http://*.example.com']
apiAllowOrigins: ["*"]
# IPs or CIDRs of proxies placed before the HTTP server.
# These proxies can use the X-Forwarded-For header to set the real IP of clients,
# and the X-Forwarded-Proto header to set the original protocol.
apiTrustedProxies: []

###############################################
# Global settings -> Metrics server

# Enable the metrics server, which allows to extract Prometheus-compatible metrics.
metrics: false
# Address of the TCP/HTTP listener.
metricsAddress: :9998
# Enable HTTPS.
metricsEncryption: false
# Path to the server key. This is needed only when encryption is yes.
# This can be generated with:
# openssl genrsa -out server.key 2048
# openssl req -new -x509 -sha256 -key server.key -out server.crt -days 3650
metricsServerKey: server.key
# Path to the server certificate.
metricsServerCert: server.crt
# Allowed CORS origins.
# Supports wildcards: ['http://*.example.com']
metricsAllowOrigins: ["*"]
# IPs or CIDRs of proxies placed before the HTTP server.
# These proxies can use the X-Forwarded-For header to set the real IP of clients,
# and the X-Forwarded-Proto header to set the original protocol.
metricsTrustedProxies: []

###############################################
# Global settings -> PPROF server

# Enable the PPROF server, which allows to extract performance reports.
pprof: false
# Address of the TCP/HTTP listener.
pprofAddress: :9999
# Enable HTTPS.
pprofEncryption: false
# Path to the server key. This is needed only when encryption is yes.
# This can be generated with:
# openssl genrsa -out server.key 2048
# openssl req -new -x509 -sha256 -key server.key -out server.crt -days 3650
pprofServerKey: server.key
# Path to the server certificate.
pprofServerCert: server.crt
# Allowed CORS origins.
# Supports wildcards: ['http://*.example.com']
pprofAllowOrigins: ["*"]
# IPs or CIDRs of proxies placed before the HTTP server.
# These proxies can use the X-Forwarded-For header to set the real IP of clients,
# and the X-Forwarded-Proto header to set the original protocol.
pprofTrustedProxies: []

###############################################
# Global settings -> Playback server

# Enable the playback server, which allows to download recordings from the server.
# The Recordings page timeline uses it (via config.cgi on the camera itself) for
# time-based playback and clip export; bound to localhost so it is not exposed
# to the network directly.
playback: yes
# Address of the TCP/HTTP listener.
playbackAddress: 127.0.0.1:9996
# Enable HTTPS.
playbackEncryption: false
# Path to the server key. This is needed only when encryption is yes.
# This can be generated with:
# openssl genrsa -out server.key 2048
# openssl req -new -x509 -sha256 -key server.key -out server.crt -days 3650
playbackServerKey: server.key
# Path to the server certificate.
playbackServerCert: server.crt
# Allowed CORS origins.
# Supports wildcards: ['http://*.example.com']
playbackAllowOrigins: ["*"]
# IPs or CIDRs of proxies placed before the HTTP server.
# These proxies can use the X-Forwarded-For header to set the real IP of clients,
# and the X-Forwarded-Proto header to set the original protocol.
playbackTrustedProxies: []

###############################################
# Global settings -> RTSP server

# Enable the RTSP server, which allows to publish and read streams with the RTSP protocol.
rtsp: true
# Enabled RTSP transport protocols. The handshake is always performed with TCP.
rtspTransports: [udp, multicast, tcp]
# Use secure protocol variants (RTSPS, SRTP, SRTCP).
# Available values are "no", "strict", "optional".
rtspEncryption: "no"
# Address of the TCP/RTSP listener. This is needed only when encryption is "no" or "optional".
rtspAddress: :8554
# Address of the TCP/RTSPS listener. This is needed only when encryption is "strict" or "optional".
rtspsAddress: :8322
# Address of the UDP/RTP listener. This is needed only when "udp" is in rtspTransports and encryption is "no" or "optional".
rtpAddress: :8000
# Address of the UDP/RTCP listener. This is needed only when "udp" is in rtspTransports and encryption is "no" or "optional".
rtcpAddress: :8001
# IP range of all UDP-multicast listeners. This is needed only when "multicast" is in rtspTransports and encryption is "no" or "optional".
multicastIPRange: 224.1.0.0/16
# Port of all UDP-multicast/RTP listeners. This is needed only when "multicast" is in rtspTransports and encryption is "no" or "optional".
multicastRTPPort: 8002
# Port of all UDP-multicast/RTCP listeners. This is needed only when "multicast" is in rtspTransports and encryption is "no" or "optional".
multicastRTCPPort: 8003
# Address of the UDP/SRTP listener. This is needed only when "udp" is in rtspTransports and encryption is "strict" or "optional".
srtpAddress: :8004
# Address of the UDP/SRTCP listener. This is needed only when "udp" is in rtspTransports and encryption is "strict" or "optional".
srtcpAddress: :8005
# Port of all UDP-multicast/SRTP listeners. This is needed only when "multicast" is in rtspTransports and encryption is "strict" or "optional".
multicastSRTPPort: 8006
# Port of all UDP-multicast/SRTCP listeners. This is needed only when "multicast" is in rtspTransports and encryption is "strict" or "optional".
multicastSRTCPPort: 8007
# Path to the server key. This is needed only when encryption is "strict" or "optional".
# This can be generated with:
# openssl genrsa -out server.key 2048
# openssl req -new -x509 -sha256 -key server.key -out server.crt -days 3650
rtspServerKey: server.key
# Path to the server certificate. This is needed only when encryption is "strict" or "optional".
rtspServerCert: server.crt
# Authentication methods. Available are "basic" and "digest".
# "digest" doesn't provide any additional security and is available for compatibility only.
rtspAuthMethods: [basic]
# IPs or CIDRs of proxies placed before the RTSP server.
# If the server receives a request from one of these entries, IP in logs
# and authentication will be taken from the PROXY protocol header.
rtspTrustedProxies: []

###############################################
# Global settings -> RTMP server

# Enable the RTMP server, which allows to publish and read streams with the RTMP protocol.
# Disabled here so a camera does not open port 1935 unless it is asked to.
rtmp: false
# Use the secure protocol variant (RTMPS).
# Available values are "no", "strict", "optional".
rtmpEncryption: "no"
# Address of the TCP/RTMP listener. This is needed only when encryption is "no" or "optional".
rtmpAddress: :1935
# Address of the TCP/RTMPS listener. This is needed only when encryption is "strict" or "optional".
rtmpsAddress: :1936
# Path to the server key. This is needed only when encryption is "strict" or "optional".
# This can be generated with:
# openssl genrsa -out server.key 2048
# openssl req -new -x509 -sha256 -key server.key -out server.crt -days 3650
rtmpServerKey: server.key
# Path to the server certificate. This is needed only when encryption is "strict" or "optional".
rtmpServerCert: server.crt
# IPs or CIDRs of proxies placed before the RTMP server.
# If the server receives a request from one of these entries, IP in logs
# and authentication will be taken from the PROXY protocol header.
rtmpTrustedProxies: []

###############################################
# Global settings -> HLS server

# Enable the HLS server, which allows to read streams with the HLS protocol.
hls: true
# Address of the TCP/HTTP listener.
hlsAddress: :8888
# Enable HTTPS.
# This is required for Low-Latency HLS to function correctly on Apple devices.
hlsEncryption: false
# Path to the server key. This is needed only when encryption is yes.
# This can be generated with:
# openssl genrsa -out server.key 2048
# openssl req -new -x509 -sha256 -key server.key -out server.crt -days 3650
hlsServerKey: server.key
# Path to the server certificate.
hlsServerCert: server.crt
# Allowed CORS origins.
# Supports wildcards: ['http://*.example.com']
hlsAllowOrigins: ["*"]
# IPs or CIDRs of proxies placed before the HLS server.
# If the server receives a request from one of these entries, IP in logs
# will be taken from the X-Forwarded-For header.
hlsTrustedProxies: []
# By default, HLS is generated only when requested by a user.
# This option allows to generate it always, avoiding the delay between request and generation.
hlsAlwaysRemux: false
# Variant of the HLS protocol to use. Available options are:
# * mpegts - uses MPEG-TS segments, for maximum compatibility.
# * fmp4 - uses fragmented MP4 segments, more efficient.
# * lowLatency - uses Low-Latency HLS.
hlsVariant: lowLatency
# Number of HLS segments to keep on the server.
# Segments allow to seek through the stream.
# Their number doesn't influence latency.
hlsSegmentCount: 7
# Minimum duration of each segment.
# A player usually puts 3 segments in a buffer before reproducing the stream.
# The final segment duration is also influenced by the interval between IDR frames,
# since the server changes the duration in order to include at least one IDR frame
# in each segment.
hlsSegmentDuration: 1s
# Minimum duration of each part.
# A player usually puts 3 parts in a buffer before reproducing the stream.
# Parts are used in Low-Latency HLS in place of segments.
# Part duration is influenced by the distance between video/audio samples
# and is adjusted in order to produce segments with a similar duration.
hlsPartDuration: 200ms
# Maximum size of each segment.
# This prevents RAM exhaustion.
hlsSegmentMaxSize: 50M
# Directory in which to save segments and non-low-latency playlists.
# This has two purposes: offloading RAM and creating a self-consistent directory
# that can be served by a CDN.
hlsDirectory: ""
# The muxer will be closed when there are no
# reader requests and this amount of time has passed.
hlsMuxerCloseAfter: 60s
# Secret to identify requests coming from a CDN.
# The CDN must insert this secret in every request in the
# 'Authorization: Bearer' header.
hlsCDNSecret: ""

###############################################
# Global settings -> WebRTC server

# Enable the WebRTC server, which allows to publish and read streams with the WebRTC protocol.
webrtc: true
# Address of the WebRTC TCP/HTTP listener.
webrtcAddress: :8889
# Enable HTTPS.
# This covers only the WebRTC handshake, while WebRTC streams
# are always encrypted with a key that is exchanged during the WebRTC handshake.
webrtcEncryption: false
# Path to the server key.
# This can be generated with:
# openssl genrsa -out server.key 2048
# openssl req -new -x509 -sha256 -key server.key -out server.crt -days 3650
webrtcServerKey: server.key
# Path to the server certificate.
webrtcServerCert: server.crt
# Allowed CORS origins.
# Supports wildcards: ['http://*.example.com']
webrtcAllowOrigins: ["*"]
# IPs or CIDRs of proxies placed before the WebRTC server.
# If the server receives a request from one of these entries, IP in logs
# will be taken from the X-Forwarded-For header.
webrtcTrustedProxies: []
# Address of a UDP/ICE listener that will receive connections.
# Use a blank string to disable.
webrtcLocalUDPAddress: :8189
# Address of a TCP/ICE listener that will receive connections.
# This is disabled by default since TCP is less efficient than UDP and
# introduces a progressive delay when network is congested.
webrtcLocalTCPAddress: ""
# WebRTC clients need to know the IP of the server.
# Gather IPs from interfaces and send them to clients.
webrtcIPsFromInterfaces: true
# Interfaces whose IPs will be sent to clients.
# An empty value means to use all available interfaces.
webrtcIPsFromInterfacesList: []
# Additional hosts or IPs to send to clients.
webrtcAdditionalHosts: []
# ICE servers. Needed only when local listeners can't be reached by clients.
# STUN servers allow to obtain and share the public IP of the server.
# TURN/TURNS servers force all traffic through them.
# MediaMTX refuses to start on an empty value here, so unlike the examples
# elsewhere this one cannot be enabled by uncommenting: replace the line
# below with the block under it.
webrtcICEServers2: []
#webrtcICEServers2:
#  - url: stun:stun.l.google.com:19302
#    # if user is "AUTH_SECRET", then authentication is secret based.
#    # the secret must be inserted into the password field.
#    username: ''
#    password: ''
#    clientOnly: false
# Maximum time to gather STUN candidates.
webrtcSTUNGatherTimeout: 5s
# Time to wait for the WebRTC handshake to complete.
webrtcHandshakeTimeout: 10s
# Maximum time to gather tracks.
webrtcTrackGatherTimeout: 2s

###############################################
# Global settings -> SRT server

# Enable the SRT server, which allows to publish and read streams with the SRT protocol.
# Disabled here so a camera does not open port 8890/udp unless it is asked to.
srt: false
# Address of the UDP/SRT listener.
srtAddress: :8890

###############################################
# MoQ server

# Enable the MoQ (Media over QUIC) server, which allows to publish and read streams with the MoQ protocol.
# Disabled here so a camera does not open ports 8892 and 8893 unless it is asked to.
moq: false
# Address of the TCP/HTTP2 listener.
# This hosts the web client.
moqHTTP2Address: :8892
# Address of the UDP/HTTP3 listener.
# This hosts the WebTransport endpoint.
moqHTTP3Address: :8892
# Address of the UDP/QUIC listener used for native MoQ-over-QUIC.
moqQUICAddress: :8893
# Path to the server key.
# This can be generated with:
# openssl genrsa -out server.key 2048
# openssl req -new -x509 -sha256 -key server.key -out server.crt -days 3650
# If set to "auto.key", a key is generated automatically and saved in the current folder.
moqServerKey: auto.key
# Path to the server certificate.
moqServerCert: auto.crt
# Allowed origins for CORS.
moqAllowOrigins: ["*"]
# Proxies which are trusted to provide the client's real IP via the X-Forwarded-For header.
moqTrustedProxies: []

###############################################
# Default path settings

# Settings in "pathDefaults" are applied anywhere,
# unless they are overridden in "paths".
pathDefaults:
  ###############################################
  # Default path settings -> General

  # Source of the stream. This can be:
  # * publisher -> the stream is provided by a RTSP, RTMP, WebRTC or SRT client
  # * rtsp://user:pass@host:port/path -> the stream is pulled from another RTSP server / camera
  # * rtsps://user:pass@host:port/path -> the stream is pulled from another RTSP server / camera with RTSPS
  # * rtsp+http://user:pass@host:port/path -> the stream is pulled from another RTSP server / camera, with HTTP tunneling
  # * rtsps+http://user:pass@host:port/path -> the stream is pulled from another RTSP server / camera, with HTTPS tunneling
  # * rtsp+ws://user:pass@host:port/path -> the stream is pulled from another RTSP server / camera, with WebSocket tunneling
  # * rtsps+ws://user:pass@host:port/path -> the stream is pulled from another RTSP server / camera, with secure WebSocket tunneling
  # * rtmp://user:pass@host:port/path#streamKey -> the stream is pulled from another RTMP server / camera
  # * rtmps://user:pass@host:port/path#streamKey -> the stream is pulled from another RTMP server / camera with RTMPS
  # * http://user:pass@host:port/stream.m3u8 -> the stream is pulled from another HLS server / camera
  # * https://user:pass@host:port/stream.m3u8 -> the stream is pulled from another HLS server / camera with HTTPS
  # * udp+mpegts://ip:port -> the stream is pulled from MPEG-TS over UDP, by listening on the specified address
  # * unix+mpegts://socketPath -> the stream is pulled from MPEG-TS over a Unix socket
  # * udp+rtp://ip:port -> the stream is pulled from RTP over UDP, by listening on the specified address
  # * srt://host:port?streamid=streamid -> the stream is pulled from another SRT server / camera
  # * moqt://user:pass@host:port/path -> the stream is pulled from another MoQ server
  # * whep://host:port/path -> the stream is pulled from another WebRTC server / camera with HTTP+WHEP
  # * wheps://host:port/path -> the stream is pulled from another WebRTC server / camera with HTTPS+WHEP
  # * redirect -> the stream is provided by another path or server
  # The following variables can be used:
  # * $MTX_QUERY: query parameters (passed by first reader)
  # * $G1, $G2, ...: regular expression groups, if path name is a regular expression.
  source: publisher
  # If the source is a URL, and the source TLS certificate is self-signed
  # or invalid, you can provide the fingerprint of the certificate in order to
  # validate it anyway. It can be obtained by running:
  # openssl s_client -connect source_ip:source_port </dev/null 2>/dev/null | sed -n '/BEGIN/,/END/p' > server.crt
  # openssl x509 -in server.crt -noout -fingerprint -sha256 | cut -d "=" -f2 | tr -d ':'
  sourceFingerprint:
  # If the source is a URL, it will be pulled only when at least
  # one reader is connected, saving bandwidth.
  sourceOnDemand: false
  # If sourceOnDemand is "yes", readers will be put on hold until the source is
  # ready or until this amount of time has passed.
  sourceOnDemandStartTimeout: 10s
  # If sourceOnDemand is "yes", the source will be closed when there are no
  # readers connected and this amount of time has passed.
  sourceOnDemandCloseAfter: 10s
  # Maximum number of readers. Zero means no limit.
  maxReaders: 0
  # SRT encryption passphrase required to read from this path.
  srtReadPassphrase:
  # Use absolute timestamp of frames, instead of replacing them with the current time.
  useAbsoluteTimestamp: false

  ###############################################
  # Default path settings -> Always available

  # Enable always-available mode, in which an offline segment is played on repeat when the stream is not available.
  alwaysAvailable: false
  # Tracks of the default offline segment.
  # Available values are: AV1, VP9, H265, H264, Opus, MPEG4Audio, G711, LPCM
  # MediaMTX refuses to start on an empty value here, so unlike the examples
  # elsewhere this one cannot be enabled by uncommenting: replace the line
  # below with the block under it.
  alwaysAvailableTracks: []
  #alwaysAvailableTracks:
  #  - codec: H264
  #    # in case of MPEG4Audio, G711, LPCM, sampleRate and ChannelCount must be provided too.
  #    sampleRate: 48000
  #    channelCount: 2
  #    # in case of G711, muLaw must be provided too.
  #    muLaw: false
  # An MP4 file can be used instead of the default offline segment.
  alwaysAvailableFile: ""

  ###############################################
  # Default path settings -> Forward

  # Forward the stream of this path to one or more external servers.
  # forward destination. This can be:
  # * rtsp://user:pass@host:port/path -> the stream is forwarded to another RTSP server
  # * rtsps://user:pass@host:port/path -> the stream is forwarded to another RTSP server with RTSPS
  # * rtmp://user:pass@host:port/path#streamKey -> the stream is forwarded to another RTMP server
  # * rtmps://user:pass@host:port/path#streamKey -> the stream is forwarded to another RTMP server with RTMPS
  # * srt://host:port?streamid=streamid -> the stream is forwarded to another SRT server
  # * whip://host:port/path/whip -> the stream is forwarded to another WebRTC server with WHIP over HTTP
  # * whips://host:port/path/whip -> the stream is forwarded to another WebRTC server with WHIP over HTTPS
  # The following variables can be used:
  # * $MTX_PATH: path name
  # * $G1, $G2, ...: regular expression groups, if path name is a regular expression.
  # MediaMTX refuses to start on an empty value here, so unlike the examples
  # elsewhere this one cannot be enabled by uncommenting: replace the line
  # below with the block under it.
  forward: []
  #forward:
  #  - dest:
  #    # If the destination TLS certificate is self-signed
  #    # or invalid, you can provide the fingerprint of the certificate in order to
  #    # validate it anyway. It can be obtained by running:
  #    # openssl s_client -connect dest_ip:dest_port </dev/null 2>/dev/null | sed -n '/BEGIN/,/END/p' > server.crt
  #    # openssl x509 -in server.crt -noout -fingerprint -sha256 | cut -d "=" -f2 | tr -d ':'
  #    destFingerprint:
  #    # Token to insert in the Authorization: Bearer header when using WHIP.
  #    whipBearerToken: ""

  ###############################################
  # Default path settings -> Record

  # Record streams to disk.
  record: false
  # Path of recording segments.
  # Extension is added automatically.
  # Available variables are %path (path name), %Y %m %d (year, month, day),
  # %H %M %S (hours, minutes, seconds), %f (microseconds), %z (time zone), %s (unix epoch).
  #
  # THIS HAS NO DEFAULT ON PURPOSE. Uncomment exactly one of the two lines
  # below by deleting its # character, choosing the one that matches your
  # product. The storage area is named after the disk, so the correct path is
  # not the same on a camera as it is on a recorder.
  #
  # Only the ".../<AREA>/root" directory is the real disk. Everything above it
  # belongs to /var/spool, which is a RAM filesystem: recording there is lost
  # on reboot and can exhaust the device's memory. If the application reports
  # only a few hundred megabytes of free space, you are on the RAM filesystem.
  #
  # Camera with an SD card:
  #recordPath: /var/spool/storage/areas/SD_DISK/root/MediaMTX/recordings/%path/%Y-%m-%d_%H-%M-%S-%f
  #
  # Recorder with an internal disk (AXIS S30 series and similar):
  #recordPath: /var/spool/storage/areas/HDD_DISK/root/MediaMTX/recordings/%path/%Y-%m-%d_%H-%M-%S-%f
  #
  # If neither matches, list the mounted areas on the device and use the one
  # you find: ls /var/spool/storage/areas/
  # Format of recorded segments.
  # Available formats are "fmp4" (fragmented MP4) and "mpegts" (MPEG-TS).
  # Keep fmp4 unless something downstream requires MPEG-TS: the playback
  # timeline, clip export and in-browser playback all need fmp4, because
  # MediaMTX's playback server does not support MPEG-TS. With mpegts the
  # Recordings page can only list, download and delete segments.
  recordFormat: fmp4
  # fMP4 segments are concatenation of small MP4 files (parts), each with this duration.
  # MPEG-TS segments are concatenation of 188-bytes packets, flushed to disk with this period.
  # When a system failure occurs, the last part gets lost.
  # Therefore, the part duration is equal to the RPO (recovery point objective).
  recordPartDuration: 1s
  # This prevents RAM exhaustion.
  recordMaxPartSize: 50M
  # Minimum duration of each segment.
  recordSegmentDuration: 1h
  # Delete segments after this timespan.
  # Set to 0s to disable automatic deletion.
  recordDeleteAfter: 1d

  ###############################################
  # Default path settings -> Publisher source (when source is "publisher")

  # Allow another client to disconnect the current publisher and publish in its place.
  overridePublisher: true
  # SRT encryption passphrase required to publish to this path.
  srtPublishPassphrase:
  # Demux MPEG-TS over RTSP into elementary streams.
  # When enabled, RTSP publishers sending MP2T/90000 will be demultiplexed
  # and their elementary streams (H.264, H.265, AAC, etc.) exposed as native tracks.
  # This allows HLS, WebRTC, and other outputs to work transparently with MPEG-TS sources.
  rtspDemuxMpegts: false

  ###############################################
  # Default path settings -> RTSP source (when source is a RTSP or a RTSPS URL)

  # Transport protocol used to pull the stream. available values are "automatic", "udp", "multicast", "tcp".
  # Upstream defaults to "automatic", which prefers UDP. On a camera pulling its
  # own sensor over 127.0.0.1 that loses RTP packets, which corrupts H264 and
  # restarts the recorder every few seconds; UDP never fails outright, so the
  # automatic fallback to TCP is never triggered. Set "automatic" back if a
  # source of yours does not support TCP.
  rtspTransport: tcp
  # Support sources that don't provide server ports or use random server ports. This is a security issue
  # and must be used only when interacting with sources that require it.
  rtspAnyPort: false
  # Range header to send to the source, in order to start streaming from the specified offset.
  # available values:
  # * clock: Absolute time
  # * npt: Normal Play Time
  # * smpte: SMPTE timestamps relative to the start of the recording
  rtspRangeType:
  # Available values:
  # * clock: UTC ISO 8601 combined date and time string, e.g. 20230812T120000Z
  # * npt: duration such as "300ms", "1.5m" or "2h45m", valid time units are "ns", "us" (or "µs"), "ms", "s", "m", "h"
  # * smpte: duration such as "300ms", "1.5m" or "2h45m", valid time units are "ns", "us" (or "µs"), "ms", "s", "m", "h"
  rtspRangeStart:
  # Scale header value to send to the source, in order to play the stream at a different speed.
  # Negative values play in reverse, values > 1 fast-forward,
  # values 0 < x < 1 play slow motion.
  rtspScale:
  # Range of ports used as source port in outgoing UDP packets.
  rtspUDPSourcePortRange: [32768, 60999]

  ###############################################
  # Default path settings -> RTP source (when source is RTP)

  # session description protocol (SDP) of the RTP stream.
  rtpSDP:

  ###############################################
  # Default path settings -> MoQ source (when source is a MoQ URL)

  # Transport protocol used to pull the stream. available values are "quic", "webtransport".
  moqTransport: quic

  ###############################################
  # Default path settings -> WebRTC / WHEP source (when source is WHEP)

  # Token to insert in the Authorization: Bearer header.
  whepBearerToken: ""
  # Maximum time to gather STUN candidates.
  whepSTUNGatherTimeout: 5s
  # Time to wait for the WebRTC handshake to complete.
  whepHandshakeTimeout: 10s
  # Maximum time to gather tracks.
  whepTrackGatherTimeout: 2s

  ###############################################
  # Default path settings -> Redirect source (when source is "redirect")

  # Path to which clients will be redirected.
  # It can be a relative path (i.e. /otherstream) or an absolute RTSP URL.
  sourceRedirect:

  ###############################################
  # Default path settings -> Hooks

  # Command to run when this path is initialized.
  # This can be used to publish a stream when the server is launched.
  # This is terminated with SIGINT when the program closes.
  # The following environment variables are available:
  # * MTX_PATH: path name
  # * RTSP_PORT: RTSP server port
  # * G1, G2, ...: regular expression groups, if path name is
  #   a regular expression.
  runOnInit:
  # Restart the command if it exits.
  runOnInitRestart: false

  # Command to run when this path is requested by a reader
  # and no one is publishing to this path yet.
  # This can be used to publish a stream on demand.
  # This is terminated with SIGINT when there are no readers anymore.
  # The following environment variables are available:
  # * MTX_PATH: path name
  # * MTX_QUERY: query parameters (passed by first reader) (url-encoded)
  # * RTSP_PORT: RTSP server port
  # * G1, G2, ...: regular expression groups, if path name is
  #   a regular expression.
  runOnDemand:
  # Restart the command if it exits.
  runOnDemandRestart: false
  # Readers will be put on hold until the runOnDemand command starts publishing
  # or until this amount of time has passed.
  runOnDemandStartTimeout: 10s
  # The command will be closed when there are no
  # readers connected and this amount of time has passed.
  runOnDemandCloseAfter: 10s
  # Command to run when there are no readers anymore.
  # Environment variables are the same as runOnDemand.
  runOnUnDemand:

  # Command to run when the stream is available to be read.
  # This is terminated with SIGINT when the stream is not available anymore.
  # The following environment variables are available:
  # * MTX_PATH: path name
  # * MTX_QUERY: query parameters (passed by publisher) (url-encoded)
  # * MTX_SOURCE_TYPE: source type
  # * MTX_SOURCE_ID: source ID
  # * RTSP_PORT: RTSP server port
  # * G1, G2, ...: regular expression groups, if path name is
  #   a regular expression.
  runOnAvailable:
  # Restart the command if it exits.
  runOnAvailableRestart: false
  # Command to run when the stream is not available anymore.
  # Environment variables are the same as runOnAvailable.
  runOnUnavailable:

  # Command to run when the stream is online, which means
  # that the stream is available and provided by a online source (not an offline segment).
  # This is terminated with SIGINT when the stream is not online anymore.
  # The following environment variables are available:
  # * MTX_PATH: path name
  # * MTX_QUERY: query parameters (passed by publisher) (url-encoded)
  # * MTX_SOURCE_TYPE: source type
  # * MTX_SOURCE_ID: source ID
  # * RTSP_PORT: RTSP server port
  # * G1, G2, ...: regular expression groups, if path name is
  #   a regular expression.
  runOnOnline:
  # Restart the command if it exits.
  runOnOnlineRestart: false
  # Command to run when the stream is not online anymore.
  # Environment variables are the same as runOnOnline.
  runOnOffline:

  # Command to run when a client starts reading.
  # This is terminated with SIGINT when a client stops reading.
  # The following environment variables are available:
  # * MTX_PATH: path name
  # * MTX_QUERY: query parameters (passed by reader) (url-encoded)
  # * MTX_READER_TYPE: reader type
  # * MTX_READER_ID: reader ID
  # * RTSP_PORT: RTSP server port
  # * G1, G2, ...: regular expression groups, if path name is
  #   a regular expression.
  runOnRead:
  # Restart the command if it exits.
  runOnReadRestart: false
  # Command to run when a client stops reading.
  # Environment variables are the same as runOnRead.
  runOnUnread:

  # Command to run when a recording segment is created.
  # The following environment variables are available:
  # * MTX_PATH: path name
  # * MTX_SEGMENT_PATH: segment file path
  # * RTSP_PORT: RTSP server port
  # * G1, G2, ...: regular expression groups, if path name is
  #   a regular expression.
  runOnRecordSegmentCreate:

  # Command to run when a recording segment is complete.
  # The following environment variables are available:
  # * MTX_PATH: path name
  # * MTX_SEGMENT_PATH: segment file path
  # * MTX_SEGMENT_DURATION: segment duration
  # * RTSP_PORT: RTSP server port
  # * G1, G2, ...: regular expression groups, if path name is
  #   a regular expression.
  runOnRecordSegmentComplete:

###############################################
# Path settings

# Settings in "paths" are applied to specific paths, and the map key
# is the name of the path.
# Any setting in "pathDefaults" can be overridden here.
# It's possible to use regular expressions by using a tilde as prefix,
# for example "~^(test1|test2)$" will match both "test1" and "test2",
# for example "~^prefix" will match all paths that start with "prefix".
paths:
  # Examples. Uncomment one by deleting its # characters and edit it, or add your own.
  #
  # This device's own sensor. Address it as 127.0.0.1 so the video never
  # leaves the device, and use a device account that is allowed to view video.
  #local:
  #  source: rtsp://user:password@127.0.0.1/axis-media/media.amp
  #
  # The same, asking the sensor for a specific codec and resolution:
  #local:
  #  source: rtsp://user:password@127.0.0.1/axis-media/media.amp?videocodec=h264&resolution=1920x1080
  #
  # Another camera on the network, such as one plugged into a recorder:
  #front_door:
  #  source: rtsp://user:password@192.168.1.50/axis-media/media.amp
  #
  # Notes:
  # * The password is stored here in plain text, so prefer an account with
  #   view-only rights. Percent-encode any special characters in it (! is %21)
  #   and put the whole URL in single quotes if it contains # or trailing
  #   spaces.
  # * "sourceOnDemand: yes" pulls video only while someone is watching, which
  #   saves power, but the source is also closed when nobody is connected, so
  #   nothing is recorded during that time. Leave it off on recorded paths.

  # Settings under path "all_others" are applied to all paths that
  # do not match another entry.
  all_others:
`;

  // ----- Configuration -------------------------------------------------------
  // The "current" config is the bundled default with recording to the SD card
  // switched on and two example paths added.

  const SAMPLE_PATHS =
    '  camera:\n' +
    '    source: rtsp://viewer:password@127.0.0.1/axis-media/media.amp?videocodec=h264&resolution=1920x1080\n' +
    '    record: yes\n' +
    '  front_door:\n' +
    '    source: rtsp://viewer:password@192.168.1.50/axis-media/media.amp?videocodec=h264&resolution=1280x720\n' +
    '    record: yes\n' +
    '\n';

  function sampleConfig() {
    return DEFAULTS_YML
      .replace('  #recordPath: /var/spool/storage/areas/SD_DISK/',
               '  recordPath: /var/spool/storage/areas/SD_DISK/')
      .replace('  recordDeleteAfter: 1d', '  recordDeleteAfter: 7d')
      .replace('  # Settings under path "all_others"', SAMPLE_PATHS + '  # Settings under path "all_others"');
  }

  // Edits survive navigation between the three pages within the tab only.
  const store = {
    get(k) { try { return sessionStorage.getItem('mtxPreview.' + k); } catch (e) { return null; } },
    set(k, v) { try { sessionStorage.setItem('mtxPreview.' + k, v); } catch (e) { /* memory only */ } },
  };
  let config = store.get('config') ?? sampleConfig();
  let backup = store.get('backup');

  // ----- Recordings ----------------------------------------------------------

  const HOUR = 3600e3, DAY = 24 * HOUR;
  const now = Date.now();
  const today = new Date(now); today.setHours(0, 0, 0, 0);
  const at = (dayOffset, h, m) => new Date(today.getTime() + dayOffset * DAY).setHours(h, m || 0, 0, 0);

  // Recorded periods per stream. Gaps: a network outage two nights ago, the
  // front door camera being off overnight, and a restart a few hours ago.
  const gap = now - 3 * HOUR;
  const STREAMS = {
    camera: { bitrate: 1.6e6, spans: [
      [at(-3, 14, 12), at(-2, 3, 5)],
      [at(-2, 5, 47), gap],
      [gap + 14 * 60e3, now],
    ] },
    front_door: { bitrate: 0.6e6, spans: [
      [at(-2, 9, 0), at(-2, 22, 10)],
      [at(-1, 6, 30), gap],
      [gap + 15 * 60e3, now],
    ] },
  };

  let seed = 20260626;
  function rand() { seed = (seed * 1103515245 + 12345) % 2147483648; return seed / 2147483648; }
  const p2 = (n, w) => String(n).padStart(w || 2, '0');
  function fileStamp(ms, micros) {
    const d = new Date(ms);
    return d.getFullYear() + '-' + p2(d.getMonth() + 1) + '-' + p2(d.getDate()) + '_' +
           p2(d.getHours()) + '-' + p2(d.getMinutes()) + '-' + p2(d.getSeconds()) + '-' + p2(micros, 6);
  }

  // MediaMTX starts a new segment on (re)connect and every recordSegmentDuration (1h).
  let segments = [];
  for (const [name, st] of Object.entries(STREAMS)) {
    st.spans = st.spans.filter(([a, b]) => b > a && a < now).map(([a, b]) => [a, Math.min(b, now)]);
    for (const [a, b] of st.spans) {
      for (let s = a; s < b; s += HOUR) {
        const e = Math.min(s + HOUR, b);
        const secs = (e - s) / 1000;
        segments.push({
          path: name + '/' + fileStamp(s, Math.floor(rand() * 1e6)) + '.mp4',
          size: Math.round(secs * st.bitrate / 8 * (0.85 + rand() * 0.3)),
          mtime: Math.floor(e / 1000),
        });
      }
    }
  }
  try {
    const deleted = JSON.parse(store.get('deleted') || '[]');
    segments = segments.filter((s) => !deleted.includes(s.path));
  } catch (e) { /* ignore */ }

  const SD_TOTAL = 124878135296;        // 128 GB microSD card as reported by statvfs
  const SD_OTHER = 1342177280;          // space used by other applications
  function storage() {
    const used = segments.reduce((n, s) => n + s.size, 0) + SD_OTHER;
    return { ok: true, total: SD_TOTAL, free: Math.max(0, SD_TOTAL - used) };
  }

  // Playback server /list: contiguous periods intersecting [start, end).
  function timeline(name, start, end) {
    const st = STREAMS[name];
    if (!st) return [];
    const a = start ? Date.parse(start) : -Infinity;
    const b = end ? Date.parse(end) : Infinity;
    return st.spans.filter(([s, e]) => e > a && s < b).map(([s, e]) => {
      const iso = new Date(s).toISOString();
      const dur = (e - s) / 1000;
      return {
        start: iso, duration: dur,
        url: 'http://127.0.0.1:9996/get?path=' + encodeURIComponent(name) +
             '&start=' + encodeURIComponent(iso) + '&duration=' + dur,
      };
    });
  }

  // ----- config.cgi ----------------------------------------------------------

  const text = (body, status) => new Response(body, {
    status: status || 200, headers: { 'Content-Type': 'text/plain; charset=utf-8' } });
  const json = (obj, status) => new Response(JSON.stringify(obj), {
    status: status || 200, headers: { 'Content-Type': 'application/json' } });

  async function handle(url, method, body) {
    const q = url.searchParams;
    const action = q.get('action') || '';
    await new Promise((r) => setTimeout(r, 120)); // feel like a network round-trip
    if (method === 'GET') {
      switch (action) {
        case '': return text(config);
        case 'defaults': return text(DEFAULTS_YML);
        case 'backup': return backup == null ? text('not found\n', 404) : text(backup);
        case 'status': return json({ ok: true, running: true, failures: 0 });
        case 'storage': return json(storage());
        case 'recordings': return json(segments);
        case 'streams': return json(Object.keys(STREAMS));
        case 'timeline': return json(timeline(q.get('path'), q.get('start'), q.get('end')));
        case 'fragidx': return json({ ok: false, error: 'playback is not available in the preview' });
        default: return json({ ok: false, error: 'not available in the preview' }, 404);
      }
    }
    if (method === 'POST') {
      if (action === 'restart') return json({ ok: true, action: 'restart' });
      if (action === 'delete') {
        const file = q.get('file');
        const before = segments.length;
        segments = segments.filter((s) => s.path !== file);
        if (segments.length === before) return json({ ok: false, error: 'not found' }, 404);
        try {
          const deleted = JSON.parse(store.get('deleted') || '[]');
          deleted.push(file);
          store.set('deleted', JSON.stringify(deleted));
        } catch (e) { /* memory only */ }
        return json({ ok: true, action: 'delete' });
      }
      if (action === '') {
        backup = config;
        config = typeof body === 'string' ? body : '';
        store.set('backup', backup);
        store.set('config', config);
        return json({ ok: true, action: 'save' });
      }
    }
    return json({ ok: false, error: 'method not allowed' }, 405);
  }

  function isBackend(u) {
    return /(^|\/)config\.cgi$/.test(u.pathname) || u.pathname.startsWith('/axis-cgi/');
  }

  const realFetch = window.fetch.bind(window);
  window.fetch = async function (input, init) {
    const req = input instanceof Request ? input : null;
    const u = new URL(req ? req.url : String(input), location.href);
    if (!isBackend(u)) return realFetch(input, init);
    const method = ((init && init.method) || (req && req.method) || 'GET').toUpperCase();
    let body = init && init.body;
    if (body == null && req && method === 'POST') body = await req.text();
    return handle(u, method, body);
  };

  // ----- Things that need a real camera ---------------------------------------

  function notice(id, msg) {
    // after the page's own status updates for the same action
    setTimeout(() => {
      const el = document.getElementById(id) || document.getElementById('status');
      if (!el) return;
      el.textContent = msg;
      el.className = '';
    }, 0);
  }

  // Video: recordings and clips would stream from the camera; keep the player empty.
  const mediaSrc = Object.getOwnPropertyDescriptor(HTMLMediaElement.prototype, 'src');
  Object.defineProperty(HTMLMediaElement.prototype, 'src', {
    configurable: true, enumerable: mediaSrc.enumerable,
    get() { return mediaSrc.get.call(this); },
    set(v) {
      if (/config\.cgi/.test(String(v))) {
        notice(/action=clip/.test(v) ? 'tlMsg' : 'status',
               'Preview: video playback needs a camera running the app.');
        return;
      }
      mediaSrc.set.call(this, v);
    },
  });

  // System log iframe on the Configuration page: show a sample log instead.
  const frameSrc = Object.getOwnPropertyDescriptor(HTMLIFrameElement.prototype, 'src');
  Object.defineProperty(HTMLIFrameElement.prototype, 'src', {
    configurable: true, enumerable: frameSrc.enumerable,
    get() { return frameSrc.get.call(this); },
    set(v) {
      if (/systemlog\.cgi/.test(String(v))) { this.srcdoc = sampleLog(); return; }
      frameSrc.set.call(this, v);
    },
  });

  function sampleLog() {
    const t0 = now - 6 * 60e3;
    const ts = (s) => {
      const d = new Date(t0 + s * 1000);
      return d.getFullYear() + '/' + p2(d.getMonth() + 1) + '/' + p2(d.getDate()) + ' ' +
             p2(d.getHours()) + ':' + p2(d.getMinutes()) + ':' + p2(d.getSeconds());
    };
    const lines = [
      [0, 'INF MediaMTX v1.21.3'],
      [0, 'INF configuration loaded from /usr/local/packages/MediaMTX/localdata/mediamtx.yml'],
      [0, 'INF [path camera] [RTSP source] started'],
      [0, 'INF [path front_door] [RTSP source] started'],
      [0, 'INF [RTSP] listener opened on :8554 (TCP), :8000 (UDP/RTP), :8001 (UDP/RTCP)'],
      [0, 'INF [RTMP] listener opened on :1935'],
      [0, 'INF [HLS] listener opened on :8888'],
      [0, 'INF [WebRTC] listener opened on :8889 (HTTP), :8189 (ICE/UDP)'],
      [0, 'INF [playback] listener opened on 127.0.0.1:9996'],
      [0, 'INF [API] listener opened on :9997'],
      [1, 'INF [path camera] [RTSP source] ready: 1 track (H264)'],
      [1, 'INF [path camera] [recorder] recording 1 track (H264)'],
      [1, 'INF [path front_door] [RTSP source] ready: 1 track (H264)'],
      [1, 'INF [path front_door] [recorder] recording 1 track (H264)'],
      [212, 'INF [HLS] [muxer camera] created automatically'],
      [213, 'INF [HLS] [muxer camera] is converting into HLS, 1 track (H264)'],
    ];
    const esc = (s) => s.replace(/[&<>]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;' }[c]));
    return '<!DOCTYPE html><meta charset="utf-8"><style>:root{color-scheme:light dark}' +
      'body{margin:8px;font:12px ui-monospace,Menlo,Consolas,monospace;white-space:pre}</style>' +
      lines.map(([s, m]) => esc(ts(s) + ' ' + m)).join('\n') +
      '\n\n(sample log for the preview)';
  }

  // Links that would leave the preview: downloads, the HLS/WebRTC players.
  function guardClick(e) {
    const a = e.target && e.target.closest && e.target.closest('a[href]');
    if (!a) return;
    let u;
    try { u = new URL(a.getAttribute('href'), location.href); } catch (err) { return; }
    if (isBackend(u)) {
      e.preventDefault();
      notice(/action=clip/.test(u.search) ? 'tlMsg' : 'status',
             'Preview: downloads need a camera running the app.');
    } else if (u.port && u.port !== location.port && u.hostname === location.hostname) {
      e.preventDefault();
      notice('status', 'Preview: the ' + a.textContent.trim() +
             ' player is served by MediaMTX on the camera.');
    }
  }
  document.addEventListener('click', guardClick, true);
  document.addEventListener('auxclick', guardClick, true);
})();
