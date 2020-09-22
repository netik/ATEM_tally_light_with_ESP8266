/*
Copyright (C) 2020 Aron N. Het Lam, aronhetlam@gmail.com

This program makes an ESP8266 into a wireless tally light system for ATEM switchers,
by using Kasper Skårhøj's (<https://skaarhoj.com>) ATEM clinet libraries for Arduino.

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

//Include libraries:
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>
#include <ATEMmin.h>
#include <TallyServer.h>

//Define LED1 color pins
#define PIN_RED1    16
#define PIN_GREEN1  4
#define PIN_BLUE1   5

#ifdef TWO_LED
//Define LED2 color pins
#define PIN_RED2    D4
#define PIN_GREEN2  D5
#define PIN_BLUE2   D6
#endif TWO_LED

//Define LED colors
#define LED_OFF     0
#define LED_RED     1
#define LED_GREEN   2
#define LED_BLUE    3
#define LED_YELLOW  4
#define LED_PINK    5
#define LED_WHITE   6

//Define states
#define STATE_STARTING                  0
#define STATE_CONNECTING_TO_WIFI        1
#define STATE_CONNECTING_TO_SWITCHER    2
#define STATE_RUNNING                   3

//Define modes of operation
#define MODE_NORMAL                     1
#define MODE_PREVIEW_STAY_ON            2
#define MODE_PROGRAM_ONLY               3

#undef TWO_LED

//Initialize global variables
ESP8266WebServer server(80);

ATEMmin atemSwitcher;

TallyServer tallyServer;

uint8_t state = STATE_STARTING;

//Define sturct for holding tally settings (mostly to simplify EEPROM read and write, in order to persist settings)
struct Settings {
    char tallyName[32] = "";
    uint8_t tallyNo;
    uint8_t tallyMode;
    bool staticIP;
    IPAddress tallyIP;
    IPAddress tallySubnetMask;
    IPAddress tallyGateway;
    IPAddress switcherIP;
};

void ledTest() {
    // cycle through LEDs to confirm function
    Serial.println("LED Test\n");
    digitalWrite(PIN_RED1, 0);
    digitalWrite(PIN_GREEN1, 0);
    digitalWrite(PIN_BLUE1, 0);
    Serial.println("Lred\n");
    digitalWrite(PIN_RED1, 1);
    delay(500);
    
    Serial.println("green\n");
    digitalWrite(PIN_RED1, 0);
    digitalWrite(PIN_GREEN1, 1);
    delay(500);

    Serial.println("blue\n");
    digitalWrite(PIN_GREEN1, 0);
    digitalWrite(PIN_BLUE1, 1);
    delay(500);
    digitalWrite(PIN_BLUE1, 0);
}


Settings settings;

bool firstRun = true;

//Perform initial setup on power on
void setup() {
    //Init pins for LED
    pinMode(PIN_RED1, OUTPUT);
    pinMode(PIN_GREEN1, OUTPUT);
    pinMode(PIN_BLUE1, OUTPUT);

#ifdef TWO_LED
    pinMode(PIN_RED2, OUTPUT);
    pinMode(PIN_GREEN2, OUTPUT);
    pinMode(PIN_BLUE2, OUTPUT);
#endif

    //Start Serial
    Serial.begin(115200);
    Serial.println("########################");
    Serial.println("Serial started");

    // confirm our build
    ledTest();

    // ok, we're good.
    setBothLEDs(LED_BLUE);

    //save flash memory from being written too without need.
    WiFi.persistent(false);

    //Read settings from EEPROM. Settings struct takes 68 bytes total (according to sizeof()). WIFI settings are stored seperately by the ESP
    EEPROM.begin(68); //Needed on ESP8266 module, as EEPROM lib works a bit differently than on a regular arduino
    EEPROM.get(0, settings);

    Serial.println(settings.tallyName);
    //Serial.println(sizeof(settings)); //Check size of settings struct
    if (settings.staticIP) {
        WiFi.config(settings.tallyIP, settings.tallyGateway, settings.tallySubnetMask);
    }

    //Put WiFi into station mode and make it connect to saved network
    WiFi.mode(WIFI_STA);
    WiFi.begin();
    WiFi.hostname(settings.tallyName);
    WiFi.setAutoReconnect(true);

    Serial.println("------------------------");
    Serial.println("Connecting to WiFi...");
    Serial.println("Network name (SSID): " + WiFi.SSID());

    // Initialize and begin HTTP server for handeling the web interface
    server.on("/", handleRoot);
    server.on("/save", handleSave);
    server.onNotFound(handleNotFound);
    server.begin();

    tallyServer.begin();

    //Wait for result from first attempt to connect - This makes sure it only activates the softAP if it was unable to connect,
    //and not just because it hasn't had the time to do so yet. It's blocking, so don't use it inside loop()
    WiFi.waitForConnectResult();

    //Set state to connecting before entering loop
    changeState(STATE_CONNECTING_TO_WIFI);
}

void loop() {
    switch (state) {
        case STATE_CONNECTING_TO_WIFI: {
                if (WiFi.status() == WL_CONNECTED) {
                    WiFi.mode(WIFI_STA); // Disable softAP if connection is successful
                    Serial.println("------------------------");
                    Serial.println("Connected to WiFi:   " + WiFi.SSID());
                    Serial.println("IP:                  " + WiFi.localIP().toString());
                    Serial.println("Subnet Mask:         " + WiFi.subnetMask().toString());
                    Serial.println("Gateway IP:          " + WiFi.gatewayIP().toString());
                    changeState(STATE_CONNECTING_TO_SWITCHER);
                } else if (firstRun) {
                    firstRun = false;
                    WiFi.mode(WIFI_AP_STA); // Enable softAP to access web interface in case of no WiFi
                    WiFi.softAP("Tally Light setup");
                    setBothLEDs(LED_WHITE);
                }
            }
            break;

        case STATE_CONNECTING_TO_SWITCHER:
            // Initialize a connection to the switcher:
            if (firstRun) {
                atemSwitcher.begin(settings.switcherIP);
                //atemSwitcher.serialOutput(0x80); //Makes Atem library print debug info
                Serial.println("------------------------");
                Serial.println("Connecting to switcher...");
                Serial.println((String)"Switcher IP:         " + settings.switcherIP[0] + "." + settings.switcherIP[1] + "." + settings.switcherIP[2] + "." + settings.switcherIP[3]);
                firstRun = false;
            }
            atemSwitcher.runLoop();
            if (atemSwitcher.hasInitialized()) {
                changeState(STATE_RUNNING);
                Serial.println("Connected to switcher");
            }
            break;

        case STATE_RUNNING:
            //Handle data exchange and connection to swithcher
            atemSwitcher.runLoop();

            int tallySources = atemSwitcher.getTallyByIndexSources();
            tallyServer.setTallySources(tallySources);
            for (int i = 0; i < tallySources; i++) {
                tallyServer.setTallyFlag(i, atemSwitcher.getTallyByIndexTallyFlags(i));
            }

            //Handle Tally Server
            tallyServer.runLoop();

            //Set tally light accordingly
            if (atemSwitcher.getTallyByIndexTallyFlags(settings.tallyNo) & 0x01) {              //if tally live
                setBothLEDs(LED_RED);
            } else if ((!(settings.tallyMode == MODE_PROGRAM_ONLY))                             //if not program only
                       && ((atemSwitcher.getTallyByIndexTallyFlags(settings.tallyNo) & 0x02)    //and tally preview
                           || settings.tallyMode == MODE_PREVIEW_STAY_ON)) {                    //or preview stay on
                setBothLEDs(LED_GREEN);
            } else {                                                                            // If tally is neither
                setBothLEDs(LED_OFF);
            }

//            //Set tally light LED 2 accordingly
//            if (atemSwitcher.getTallyByIndexTallyFlags(settings.tallyNo) & 0x01) {              //if tally live
//                setLED2(LED_RED);
//            } else if ((!(settings.tallyMode == MODE_PROGRAM_ONLY))                             //if not program only
//                       && ((atemSwitcher.getTallyByIndexTallyFlags(settings.tallyNo) & 0x02)    //and tally preview
//                           || settings.tallyMode == MODE_PREVIEW_STAY_ON)) {                    //or preview stay on
//                setLED2(LED_GREEN);
//            } else {                                                                            // If tally is neither
//                setLED2(LED_OFF);
//            }

            //Switch state if connection is lost, dependant on which connection is lost.
            if (WiFi.status() != WL_CONNECTED) {
                Serial.println("------------------------");
                Serial.println("WiFi connection lost...");
                changeState(STATE_CONNECTING_TO_WIFI);

                //Force atem library to reset connection, in order for status to read correctly on website.
                atemSwitcher.begin(settings.switcherIP);

                //Reset tally server's tally flags, They won't get the message, but it'll be reset for when the connectoin is back.
                tallyServer.resetTallyFlags();

            } else if (!atemSwitcher.hasInitialized()) { // will return false if the connection was lost
                Serial.println("------------------------");
                Serial.println("Connection to Switcher lost...");
                changeState(STATE_CONNECTING_TO_SWITCHER);

                //Reset tally server's tally flags, so clients turn off their lights.
                tallyServer.resetTallyFlags();
            }
            break;
    }

    //Handle web interface
    server.handleClient();
}

void changeState(uint8_t stateToChangeTo) {
    firstRun = true;
    switch (stateToChangeTo) {
        case STATE_CONNECTING_TO_WIFI:
            state = STATE_CONNECTING_TO_WIFI;
            setBothLEDs(LED_BLUE);
            break;
        case STATE_CONNECTING_TO_SWITCHER:
            state = STATE_CONNECTING_TO_SWITCHER;
            setBothLEDs(LED_PINK);
            break;
        case STATE_RUNNING:
            state = STATE_RUNNING;
            setBothLEDs(LED_GREEN);
            break;
    }
}

void setBothLEDs(uint8_t color) {
    setLED(color, PIN_RED1, PIN_GREEN1, PIN_BLUE1);
#ifdef TWO_LED
    setLED(color, PIN_RED2, PIN_GREEN2, PIN_BLUE2);
#endif
}

void setLED1(uint8_t color) {
    setLED(color, PIN_RED1, PIN_GREEN1, PIN_BLUE1);
}

#ifdef TWO_LED
void setLED2(uint8_t color) {
    setLED(color, PIN_RED2, PIN_GREEN2, PIN_BLUE2);
}
#endif

void setLED(uint8_t color, int pinRed, int pinGreen, int pinBlue) {
    switch (color) {
        case LED_OFF:
            digitalWrite(pinRed, 0);
            digitalWrite(pinGreen, 0);
            digitalWrite(pinBlue, 0);
            break;
        case LED_RED:
            digitalWrite(pinRed, 1);
            digitalWrite(pinGreen, 0);
            digitalWrite(pinBlue, 0);
            break;
        case LED_GREEN:
            digitalWrite(pinRed, 0);
            digitalWrite(pinGreen, 1);
            digitalWrite(pinBlue, 0);
            break;
        case LED_BLUE:
            digitalWrite(pinRed, 0);
            digitalWrite(pinGreen, 0);
            digitalWrite(pinBlue, 1);
            break;
        case LED_YELLOW:
            digitalWrite(pinRed, 1);
            digitalWrite(pinGreen, 1);
            digitalWrite(pinBlue, 0);
            break;
        case LED_PINK:
            digitalWrite(pinRed, 1);
            digitalWrite(pinGreen, 0);
            analogWrite(pinBlue, 0xff);
            break;
        case LED_WHITE:
            digitalWrite(pinRed, 1);
            digitalWrite(pinGreen, 1);
            digitalWrite(pinBlue, 1);
            break;
    }
}

//Serve setup web page to client, by sending HTML with the correct variables
void handleRoot() {
    String html = "<!DOCTYPE html> <html> <head> <meta charset=\"ASCII\"> <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\"> <title>Tally Light setup</title> </head> <script> function switchIpField(e) { console.log(\"switch\"); console.log(e); var target = e.srcElement || e.target; var maxLength = parseInt(target.attributes[\"maxlength\"].value, 10); var myLength = target.value.length; if (myLength >= maxLength) { var next = target.nextElementSibling; while (next != null) { if (next.className.includes(\"IP\")) { next.focus(); break; } next = next.nextElementSibling } } else if (myLength == 0) { var previous = target.previousElementSibling; while (previous != null) { if (previous.className.includes(\"IP\")) { previous.focus(); break; } previous = previous.previousElementSibling; } } } function ipFieldFocus(e) { console.log(\"focus\"); console.log(e); var target = e.srcElement || e.target; target.select(); } function load() { var containers = document.getElementsByClassName(\"IP\"); for (var i = 0; i < containers.length; i++) { var container = containers[i]; container.oninput = switchIpField; container.onfocus = ipFieldFocus; } containers = document.getElementsByClassName(\"tIP\"); for (var i = 0; i < containers.length; i++) { var container = containers[i]; container.oninput = switchIpField; container.onfocus = ipFieldFocus; } toggleStaticIPFields(); } function toggleStaticIPFields() { var enabled = document.getElementById(\"staticIP\").checked; document.getElementById(\"staticIPHidden\").disabled = enabled; var staticIpFields = document.getElementsByClassName('tIP'); for (var i = 0; i < staticIpFields.length; i++) { staticIpFields[i].disabled = !enabled; } } </script> <body style=\"font-family:Verdana; white-space:nowrap;\" onload=\"load()\"> <table bgcolor=\"#777777\" border=\"0\" width=\"100%\" cellpadding=\"1\" style=color:#ffffff;font-size:12px;\"> <tr> <td> <h1>Tally Light setup</h1> </td> </tr> <tr> <td> <h2>Status:</h2> </td> </tr> </table><br> <table> <tr> <td>Connection Status:</td> <td>";
    switch (WiFi.status()) {
        case WL_CONNECTED:
            html += "Connected to network";
            break;
        case WL_NO_SSID_AVAIL:
            html += "Network not found";
            break;
        case WL_CONNECT_FAILED:
            html += "Invalid password";
            break;
        case WL_IDLE_STATUS:
            html += "Changing state...";
            break;
        case WL_DISCONNECTED:
            html += "Station mode disabled";
            break;
        case -1:
            html += "Timeout";
            break;
    }

    html += "</td> </tr> <tr> <td>Network name (SSID):</td> <td>";
    html += WiFi.SSID();
    html += "</td> </tr> <tr> <td><br></td> </tr> <tr> <td>Signal strength:</td> <td>";
    html += WiFi.RSSI();
    html += " dBm</td> </tr> <tr> <td>Static IP:</td> <td>";
    html += settings.staticIP == true ? "True" : "False";
    html += "</td> </tr> <tr> <td>Tally Light IP:</td> <td>";
    html += WiFi.localIP().toString();
    html += "</td> </tr> <tr> <td>Subnet mask: </td> <td>";
    html += WiFi.subnetMask().toString();
    html += "</td> </tr> <tr> <td>Gateway: </td> <td>";
    html += WiFi.gatewayIP().toString();
    html += "</td> </tr> <tr> <td><br></td> </tr> <tr> <td>ATEM switcher status:</td> <td>";
    if (atemSwitcher.hasInitialized())
        html += "Connected - Initialized";
    else if (atemSwitcher.isConnected())
        html += "Connected - Wating for initialization - Connection might have been rejected";
    else if (WiFi.status() == WL_CONNECTED)
        html += "Disconnected - No response from switcher";
    else
        html += "Disconnected - Waiting for WiFi";
    html += "</td> </tr> <tr> <td>ATEM switcher IP:</td> <td>";
    html += (String)settings.switcherIP[0] + '.' + settings.switcherIP[1] + '.' + settings.switcherIP[2] + '.' + settings.switcherIP[3];
    html += "</td> </tr> </table><br> <table bgcolor=\"#777777\" border=\"0\" width=\"100%\" cellpadding=\"1\" style=\"color:#ffffff;font-size:12px;\"> <td> <h2>Settings:</h2> </td> </tr> </table><br> <table> <form action=\"/save\" method=\"post\"> <tr> <td>Tally Light name: </td> <td><input type=\"text\" size=\"30\" maxlength=\"30\" name=\"tName\" value=\"";
    html += WiFi.hostname();
    html += "\" required> <tr> <td>Tally Light mode: </td> <td><select name = \"tMode\"> <option value=\"";
    html += (String) MODE_NORMAL + "\" ";
    if (settings.tallyMode == MODE_NORMAL)
        html += "selected";
    html += ">Normal</option> <option value=\"";
    html += (String) MODE_PREVIEW_STAY_ON + "\" ";
    if (settings.tallyMode == MODE_PREVIEW_STAY_ON)
        html += "selected";
    html += ">Preview stay on</option> <option value=\"";
    html += (String) MODE_PROGRAM_ONLY + "\" ";
    if (settings.tallyMode == MODE_PROGRAM_ONLY)
        html += "selected";
    html += ">Program only</option> </select> </td> </tr> </td> </tr> <tr> <td>Tally Light number: </td> <td><input type=\"number\" size=\"5\" min=\"1\" max=\"21\" name=\"tNo\" value=\"";
    html += (settings.tallyNo + 1);
    html += "\" required> </td> </tr> <tr> <td><br></td> </tr> <tr> <td>Network name (SSID): </td> <td><input type=\"text\" size=\"30\" maxlength=\"30\" name=\"ssid\" value=\"";
    html += WiFi.SSID();
    html += "\" required> </td> </tr> <tr> <td>Network password: </td> <td><input type=\"password\" size=\"30\" maxlength=\"30\" name=\"pwd\"  pattern=\"^$|.{8,32}\" value=\"";
    if (WiFi.isConnected()) //As a minimum security meassure, to only send the wifi password if it's currently connected to the given network.
        html += WiFi.psk();
    html += "\"> </td> </tr> <tr> <td><br></td> </tr> <tr> <td>Use static IP: </td> <td> <input type=\"hidden\" id=\"staticIPHidden\" name=\"staticIP\" value=\"false\"/> <input id=\"staticIP\" type=\"checkbox\" name=\"staticIP\" value=\"true\" onchange=\"toggleStaticIPFields()\" ";
    if (settings.staticIP)
        html += "checked";
    html += "/> </td> </tr> <tr> <td>Tally Light IP: </td> <td><input class=\"tIP\" type=\"text\" size=\"3\" maxlength=\"3\" name=\"tIP1\" pattern=\"\\d{0,3}\" value=\"";
    html += settings.tallyIP[0];
    html += "\" required>. <input class=\"tIP\" type=\"text\" size=\"3\" maxlength=\"3\" name=\"tIP2\" pattern=\"\\d{0,3}\" value=\"";
    html += settings.tallyIP[1];
    html += "\" required>. <input class=\"tIP\" type=\"text\" size=\"3\" maxlength=\"3\" name=\"tIP3\" pattern=\"\\d{0,3}\" value=\"";
    html += settings.tallyIP[2];
    html += "\" required>. <input class=\"tIP\" type=\"text\" size=\"3\" maxlength=\"3\" name=\"tIP4\" pattern=\"\\d{0,3}\" value=\"";
    html += settings.tallyIP[3];
    html += "\" required> </td> </tr> <tr> <td>Subnet mask: </td> <td><input class=\"tIP\" type=\"text\" size=\"3\" maxlength=\"3\" name=\"mask1\" pattern=\"\\d{0,3}\" value=\"";
    html += settings.tallySubnetMask[0];
    html += "\" required>. <input class=\"tIP\" type=\"text\" size=\"3\" maxlength=\"3\" name=\"mask2\" pattern=\"\\d{0,3}\" value=\"";
    html += settings.tallySubnetMask[1];
    html += "\" required>. <input class=\"tIP\" type=\"text\" size=\"3\" maxlength=\"3\" name=\"mask3\" pattern=\"\\d{0,3}\" value=\"";
    html += settings.tallySubnetMask[2];
    html += "\" required>. <input class=\"tIP\" type=\"text\" size=\"3\" maxlength=\"3\" name=\"mask4\" pattern=\"\\d{0,3}\" value=\"";
    html += settings.tallySubnetMask[3];
    html += "\" required> </td> </tr> <tr> <td>Gateway: </td> <td><input class=\"tIP\" type=\"text\" size=\"3\" maxlength=\"3\" name=\"gate1\" pattern=\"\\d{0,3}\" value=\"";
    html += settings.tallyGateway[0];
    html += "\" required>. <input class=\"tIP\" type=\"text\" size=\"3\" maxlength=\"3\" name=\"gate2\" pattern=\"\\d{0,3}\" value=\"";
    html += settings.tallyGateway[1];
    html += "\" required>. <input class=\"tIP\" type=\"text\" size=\"3\" maxlength=\"3\" name=\"gate3\" pattern=\"\\d{0,3}\" value=\"";
    html += settings.tallyGateway[2];
    html += "\" required>. <input class=\"tIP\" type=\"text\" size=\"3\" maxlength=\"3\" name=\"gate4\" pattern=\"\\d{0,3}\" value=\"";
    html += settings.tallyGateway[3];
    html += "\" required> </td> </tr> <tr> <td><br></td> </tr> <tr> <td>ATEM switcher IP: </td> <td><input class=\"IP\" type=\"text\" size=\"3\" maxlength=\"3\" name=\"aIP1\" pattern=\"\\d{0,3}\" value=\"";
    html += settings.switcherIP[0];
    html += "\" required>. <input class=\"IP\" type=\"text\" size=\"3\" maxlength=\"3\" name=\"aIP2\" pattern=\"\\d{0,3}\" value=\"";
    html += settings.switcherIP[1];
    html += "\" required>. <input class=\"IP\" type=\"text\" size=\"3\" maxlength=\"3\" name=\"aIP3\" pattern=\"\\d{0,3}\" value=\"";
    html += settings.switcherIP[2];
    html += "\" required>. <input class=\"IP\" type=\"text\" size=\"3\" maxlength=\"3\" name=\"aIP4\" pattern=\"\\d{0,3}\" value=\"";
    html += settings.switcherIP[3];
    html += "\" required> </tr> <tr> <td><br></td> </tr> <tr> <td /> <td style=\"float: right;\"> <input type=\"submit\" value=\"Save Changes\" /> </td> </tr> </form> </table> </body> </html>";

    server.send(200, "text/html", html);
}

//Save new settings from client in EEPROM and restart the ESP8266 module
void handleSave() {
    if (server.method() != HTTP_POST) {
        server.send(405, "text/html", "<!DOCTYPE html> <html> <head> <meta charset=\"ASCII\"> <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\"> <title>Tally Light setup</title> </head> <body style=\"font-family:Verdana;\"> <table bgcolor=\"#777777\" border=\"0\" width=\"100%\" cellpadding=\"1\" style=\"color:#ffffff;font-size:12px;\"> <tr> <td> <h1>&nbsp Tally Light setup</h1> </td> </tr> </table><br>Request without posting settings not allowed</body></html>");
    } else {
        String ssid;
        String pwd;
        bool change = false;
        for (uint8_t i = 0; i < server.args(); i++) {
            change = true;
            String var = server.argName(i);
            String val = server.arg(i);

            if (var ==  "tName") {
                val.toCharArray(settings.tallyName, (uint8_t)32);
            } else if (var ==  "tMode") {
                settings.tallyMode = val.toInt();
            } else if (var ==  "tNo") {
                settings.tallyNo = val.toInt() - 1;
            } else if (var ==  "ssid") {
                ssid = String(val);
            } else if (var ==  "pwd") {
                pwd = String(val);
            } else if (var ==  "staticIP") {
                settings.staticIP = (val == "true");
            } else if (var ==  "tIP1") {
                settings.tallyIP[0] = val.toInt();
            } else if (var ==  "tIP2") {
                settings.tallyIP[1] = val.toInt();
            } else if (var ==  "tIP3") {
                settings.tallyIP[2] = val.toInt();
            } else if (var ==  "tIP4") {
                settings.tallyIP[3] = val.toInt();
            } else if (var ==  "mask1") {
                settings.tallySubnetMask[0] = val.toInt();
            } else if (var ==  "mask2") {
                settings.tallySubnetMask[1] = val.toInt();
            } else if (var ==  "mask3") {
                settings.tallySubnetMask[2] = val.toInt();
            } else if (var ==  "mask4") {
                settings.tallySubnetMask[3] = val.toInt();
            } else if (var ==  "gate1") {
                settings.tallyGateway[0] = val.toInt();
            } else if (var ==  "gate2") {
                settings.tallyGateway[1] = val.toInt();
            } else if (var ==  "gate3") {
                settings.tallyGateway[2] = val.toInt();
            } else if (var ==  "gate4") {
                settings.tallyGateway[3] = val.toInt();
            } else if (var ==  "aIP1") {
                settings.switcherIP[0] = val.toInt();
            } else if (var ==  "aIP2") {
                settings.switcherIP[1] = val.toInt();
            } else if (var ==  "aIP3") {
                settings.switcherIP[2] = val.toInt();
            } else if (var ==  "aIP4") {
                settings.switcherIP[3] = val.toInt();
            }
        }

        if (change) {
            EEPROM.put(0, settings);
            EEPROM.commit();

            server.send(200, "text/html", (String)"<!DOCTYPE html> <html> <head> <meta charset=\"ASCII\"> <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\"> <title>Tally Light setup</title> </head> <body> <table bgcolor=\"#777777\" border=\"0\" width=\"100%\" cellpadding=\"1\" style=\"font-family:Verdana;color:#ffffff;font-size:12px;\"> <tr> <td> <h1>&nbsp Tally Light setup</h1> </td> </tr> </table><br>Settings saved successfully.</body></html>");

            //Delay to let data be saved, and the responce to be sent properly to the client
            delay(5000);

            if (ssid && pwd && (ssid != WiFi.SSID() || pwd != WiFi.psk())) {
                WiFi.persistent(true);
                WiFi.begin(ssid, pwd);
                WiFi.persistent(false);
            }

            ESP.restart();
        }
    }
}

//Send 404 to client in case of invalid webpage being requested.
void handleNotFound() {
    server.send(404, "text/html", "<!DOCTYPE html> <html> <head> <meta charset=\"ASCII\"> <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\"> <title>Tally Light setup</title> </head> <body style=\"font-family:Verdana;\"> <table bgcolor=\"#777777\" border=\"0\" width=\"100%\" cellpadding=\"1\" style=\"color:#ffffff;font-size:12px;\"> <tr> <td> <h1>&nbsp Tally Light setup</h1> </td> </tr> </table><br>404 - Page not found</body></html>");
}
