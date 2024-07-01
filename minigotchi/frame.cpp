/**
 * frame.cpp: handles the sending of "pwnagotchi" beacon frames
 */

#include "frame.h"

/** developer note:
 *
 * when it comes to detecting a pwnagotchi, this is done with pwngrid/opwngrid.
 * essentially pwngrid looks for the numbers 222-226 in payloads, and if they
 * aren't there, it ignores it. these need to be put into the frames!!!
 *
 * note that these frames aren't just normal beacon frames, rather a modified
 * one with data, additional ids, etc. frames are dynamically constructed,
 * headers are included like a normal frame. by far this is the most memory
 * heaviest part of the minigotchi, the reason is
 *
 */

// initializing
size_t Frame::payloadSize = 255; // by default
const size_t Frame::chunkSize = 0xFF;
bool Frame::sent = false;

// beacon stuff
uint8_t Frame::beaconFrame[2048];
size_t Frame::essidLength = 0;
uint8_t Frame::headerLength = 0;

// payload ID's according to pwngrid
const uint8_t Frame::IDWhisperPayload = 0xDE;
const uint8_t Frame::IDWhisperCompression = 0xDF;
const uint8_t Frame::IDWhisperIdentity = 0xE0;
const uint8_t Frame::IDWhisperSignature = 0xE1;
const uint8_t Frame::IDWhisperStreamHeader = 0xE2;

// other addresses
const uint8_t Frame::SignatureAddr[] = {0xde, 0xad, 0xbe, 0xef, 0xde, 0xad};
const uint8_t Frame::BroadcastAddr[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
const uint16_t Frame::wpaFlags = 0x0411;

const uint8_t Frame::header[]{
    /*  0 - 1  */ 0x80,
    0x00, // frame control, beacon frame
    /*  2 - 3  */ 0x00,
    0x00, // duration
    /*  4 - 9  */ 0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff, // broadcast address
    /* 10 - 15 */ 0xde,
    0xad,
    0xbe,
    0xef,
    0xde,
    0xad, // source address
    /* 16 - 21 */ 0xa1,
    0x00,
    0x64,
    0xe6,
    0x0b,
    0x8b, // bssid
    /* 22 - 23 */ 0x40,
    0x43, // fragment and sequence number
    /* 24 - 32 */ 0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00, // timestamp
    /* 33 - 34 */ 0x64,
    0x00, // interval
    /* 35 - 36 */ 0x11,
    0x04, // capability info
};

// get header length
const int Frame::pwngridHeaderLength = sizeof(Frame::header);

/** developer note:
 *
 * frame structure based on how it was built here
 *
 * 1. header
 * 2. payload id's
 * 3. (chunked) pwnagotchi data
 *
 */

/** developer note:
 *
 * referenced the following for packing-related function:
 *
 * https://github.com/evilsocket/pwngrid/blob/master/wifi/pack.go
 *
 */

void Frame::pack() {
  // make a json doc
  String jsonString = "";
  DynamicJsonDocument doc(2048);

  doc["epoch"] = Config::epoch;
  doc["face"] = Config::face;
  doc["identity"] = Config::identity;
  doc["name"] = Config::name;

  JsonObject policy = doc.createNestedObject("policy");
  policy["advertise"] = Config::advertise;
  policy["ap_ttl"] = Config::ap_ttl;
  policy["associate"] = Config::associate;
  policy["bored_num_epochs"] = Config::bored_num_epochs;

  JsonArray channels = policy.createNestedArray("channels");
  for (size_t i = 0; i < sizeof(Config::channels) / sizeof(Config::channels[0]);
       ++i) {
    channels.add(Config::channels[i]);
  }

  policy["deauth"] = Config::deauth;
  policy["excited_num_epochs"] = Config::excited_num_epochs;
  policy["hop_recon_time"] = Config::hop_recon_time;
  policy["max_inactive_scale"] = Config::max_inactive_scale;
  policy["max_interactions"] = Config::max_interactions;
  policy["max_misses_for_recon"] = Config::max_misses_for_recon;
  policy["min_recon_time"] = Config::min_rssi;
  policy["min_rssi"] = Config::min_rssi;
  policy["recon_inactive_multiplier"] = Config::recon_inactive_multiplier;
  policy["recon_time"] = Config::recon_time;
  policy["sad_num_epochs"] = Config::sad_num_epochs;
  policy["sta_ttl"] = Config::sta_ttl;

  doc["pwnd_run"] = Config::pwnd_run;
  doc["pwnd_tot"] = Config::pwnd_tot;
  doc["session_id"] = Config::session_id;
  doc["uptime"] = Config::uptime;
  doc["version"] = Config::version;

  // serialize then put into beacon frame
  serializeJson(doc, jsonString);
  Frame::essidLength = measureJson(doc);
  Frame::headerLength = 2 + ((uint8_t)(essidLength / 255) * 2);

  size_t newLength =
      Frame::pwngridHeaderLength + Frame::essidLength + Frame::headerLength;
  memcpy(Frame::beaconFrame, Frame::header, Frame::pwngridHeaderLength);

  /** developer note:
   *
   * if you literally want to check the json everytime you send a packet(non
   * serialized ofc)
   *
   * Serial.println(jsonString);
   */

  int currentByte = pwngridHeaderLength;

  for (int i = 0; i < Frame::essidLength; i++) {
    if (i == 0 || i % 255 == 0) {
      Frame::beaconFrame[currentByte++] = Frame::IDWhisperPayload;
      if (Frame::essidLength - i < Frame::chunkSize) {
        Frame::payloadSize = Frame::essidLength - i;
      }
      Frame::beaconFrame[currentByte++] = Frame::payloadSize;
    }

    uint8_t nextByte = (uint8_t)'?';
    if (isAscii(jsonString[i])) {
      nextByte = (uint8_t)jsonString[i];
    }

    Frame::beaconFrame[currentByte++] = nextByte;
  }

  /* Uncomment if you want to test beacon frames

  Serial.println("('-') Full Beacon Frame:");
  for (size_t i = 0; i < sizeof(Frame::beaconFrame); ++i) {
    Serial.print(beaconFrame[i], HEX);
    Serial.print(" ");
  }
  Serial.println(" ");

  */
}

bool Frame::send() {
  // build frame
  Frame::pack();
  size_t totalLength =
      Frame::pwngridHeaderLength + Frame::essidLength + Frame::headerLength;

  // send full frame
  // we dont use raw80211 since it sends a header(which we don't need), although
  // we do use it for monitoring, etc.
  Frame::sent = wifi_send_pkt_freedom(Frame::beaconFrame, totalLength, 0);
  delay(102);
  return (Frame::sent == 0);
}

void Frame::advertise() {
  int packets = 0;
  unsigned long startTime = millis();

  if (Config::advertise) {
    Serial.println("(>-<) Starting advertisment...");
    Serial.println(" ");
    Display::updateDisplay("(>-<)", "Starting advertisment...");
    Minigotchi::monStart();
    Parasite::sendAdvertising();
    delay(Config::shortDelay);
    for (int i = 0; i < 150; ++i) {
      if (Frame::send()) {
        packets++;

        // calculate packets per second
        float pps = packets / (float)(millis() - startTime) * 1000;

        // show pps
        if (!isinf(pps)) {
          Serial.print("(>-<) Packets per second: ");
          Serial.print(pps);
          Serial.print(" pkt/s (Channel: ");
          Serial.print(Channel::getChannel());
          Serial.println(")");
          Display::updateDisplay(
              "(>-<)", "Packets per second: " + (String)pps + " pkt/s" +
                           "(Channel: " + (String)Channel::getChannel() + ")");
        }
      } else {
        Serial.println("(X-X) Advertisment failed to send!");
      }
    }

    Serial.println(" ");
    Serial.println("(^-^) Advertisment finished!");
    Serial.println(" ");
    Display::updateDisplay("(^-^)", "Advertisment finished!");
  } else {
    // do nothing but still idle
  }
}
