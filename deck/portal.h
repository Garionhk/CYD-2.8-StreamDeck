// ===========================================================================
// portal.h -- WiFi, and the setup web page
// ===========================================================================
// Patterned on the WOL-NTFY webconfig (one page, every mode) and the MP3
// player's upload mode (open hotspot + DNS that answers every name, so a phone
// pops the page up by itself):
//
//   no SSID saved      hotspot "CYD-Deck-XXXX" + captive page at 192.168.4.1
//   SSID saved         joins it; the same page is served on the LAN address
//   join fails (20 s)  the hotspot comes up as well, while the station keeps
//                      retrying -- so a moved router never locks you out
//
// On the LAN the page also answers by name: http://<device id>.local (mDNS),
// e.g. http://cyd-a1b2c3.local, so nobody has to find the IP. The device menu
// shows both, with a QR code.
//
// Saving the page writes settings (NVS + the SD card's config.json) and, when
// a network or Companion field changed, restarts. Brightness and colour
// inversion apply live.
//
// Login: HTTP Basic, user "admin", as the WOL-NTFY settings page. There is no
// password until the owner sets one on the page (the first-boot hotspot page
// included); until then the page is open and says so in a banner. Five wrong
// passwords lock it for 5 s, doubling with each further miss up to 5 min.
// Forgotten: the device menu's Web setup screen clears it (physical access is
// the owner), or blank "web.password" in config.json on the SD card.
//
// The page never sends a stored password -- WiFi or its own -- back to the
// browser. Basic auth over plain HTTP is readable by anyone sniffing the same
// network: it keeps casual hands off, it is not encryption.
#pragma once

#include <Arduino.h>

void   portal_begin();
void   portal_tick();
bool   portal_apActive();
bool   portal_wifiUp();          // joined the configured network
String portal_apName();
String portal_url();             // "http://<LAN address>", or the hotspot's
String portal_mdnsUrl();         // "http://cyd-a1b2c3.local" once joined, else empty

bool   portal_hasPassword();
void   portal_clearPassword();   // the device menu's recovery: page open again
