#pragma once

/* @note Set by main() when the server is launched with --verbose / -v.
   Diagnostic logging (payloads, packet dumps, request sizes) is gated on this.
   Auth-relevant failures (bad password, malformed login) are always logged. */
extern bool g_verbose;
