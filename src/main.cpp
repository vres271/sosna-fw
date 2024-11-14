#include <Arduino.h>

#ifdef ESP32
#include <WiFi.h>
#include <AsyncTCP.h>
#elif defined(ESP8266)
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#endif
#include <ESPAsyncWebServer.h>

AsyncWebServer server(80);
const char* PARAM_LED = "led";
const char* PARAM_R = "r";
const char* PARAM_G = "g";
const char* PARAM_B = "b";



#define WIFI_SSID "TP-LINK_B97598"
#define WIFI_PASS "sukaher271"

#include <FastLED.h>

#define NUM_LEDS 200
#define LED_PIN 5      // пин ленты
#define LED_TYPE WS2812B // чип ленты
#define LED_ORDER GRB   // порядок цветов ленты

// Define the array of leds
CRGB leds[NUM_LEDS];
byte counter = 0;
long unsigned lastLedShow = 0;
long unsigned lastCounterSwitch = 0;

struct GColor {
  uint8_t r;
  uint8_t g;
  uint8_t b;
};

struct GPoint {
  long unsigned t;
  GColor color;
};

struct GVector {
  GPoint points[16];
};

GVector vectors[NUM_LEDS];


void notFound(AsyncWebServerRequest *request) {
    request->send(404, "text/plain", "Not found");
    Serial.println("Not found");
}

void setup() {
    Serial.begin(115200);

    FastLED.addLeds<LED_TYPE, LED_PIN, LED_ORDER>(leds, NUM_LEDS).setCorrection(0xFF80F0);

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    if (WiFi.waitForConnectResult() != WL_CONNECTED) {
        Serial.printf("WiFi Failed!\n");
        return;
    }

    Serial.print("");
    Serial.println("IP Address: ");
    Serial.println(WiFi.localIP());

    server.on("/get", HTTP_GET, [] (AsyncWebServerRequest *request) {
        String message;
        int unsigned ledN;
        uint8_t r;
        uint8_t g;
        uint8_t b;
        if (request->hasParam(PARAM_LED)) {
            ledN = (request->getParam(PARAM_LED)->value()).toInt();
            r = (request->getParam(PARAM_R)->value()).toInt();
            g = (request->getParam(PARAM_G)->value()).toInt();
            b = (request->getParam(PARAM_B)->value()).toInt();

            vectors[ledN] = (GVector) {{
                (GPoint) {1, (GColor) {r, g, b}},
                (GPoint) {3000, (GColor) {255, 0, 0}},
                (GPoint) {6000, (GColor) {0, 0, 255}},
            }};
            
            message = "{\"led\": \"" + String(ledN) + "\"}";
        } else {
            message = "{\"error\": \"Empty params\"}";
        }
        AsyncWebServerResponse *response = request->beginResponse(200, "application/json", message);
        response->addHeader("Access-Control-Allow-Origin","*");
        request->send(response);
        Serial.println(message);
    });

    // Send a POST request to <IP>/post with a form field message set to <message>
    server.on("/post", HTTP_POST, [](AsyncWebServerRequest *request){
        String message;
        if (request->hasParam(PARAM_LED, true)) {
            message = request->getParam(PARAM_LED, true)->value();
        } else {
            message = "No message sent";
        }
        request->send(200, "text/plain", "Hello, POST: " + message);
    });

    server.onNotFound(notFound);

    server.begin();

}

void loop() {
    if (millis() > lastLedShow + 1000) {
        for (int i = 0; i < NUM_LEDS; i++) {
            Serial.println();
            if (sizeof(vectors[i].points) > 0) {
                leds[i] = CRGB(
                    vectors[i].points[counter].color.r, 
                    vectors[i].points[counter].color.g, 
                    vectors[i].points[counter].color.b
                );
                Serial.print(i);
                Serial.print(" - ");
                Serial.print(vectors[i].points[counter].color.r); Serial.print(", ");
                Serial.print(vectors[i].points[counter].color.g); Serial.print(", ");
                Serial.print(vectors[i].points[counter].color.b); Serial.print(", ");
                Serial.print(", ");
            }
        }
        FastLED.show();
        lastLedShow = millis();
    }
    if (millis() > lastCounterSwitch + 2000) {
        counter++; 
        if (counter > 2) {
            counter = 0;
        }
        lastCounterSwitch = millis();
    }
}
