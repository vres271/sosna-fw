#include <Arduino.h>
#include <utils.cpp>
// #include <files.cpp>

#ifdef ESP32
#include <WiFi.h>
#include <AsyncTCP.h>
#elif defined(ESP8266)
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#endif
#include <ESPAsyncWebServer.h>

#include <FS.h>

AsyncWebServer server(80);
const char* PARAM_LED = "led";
const char* PARAM_R = "r";
const char* PARAM_G = "g";
const char* PARAM_B = "b";

#define WIFI_SSID "TP-Link_49CB"
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
  byte timeFn;
  byte orderFn;
  GColor color;
};

struct GVector {
  GPoint points[16];
  long unsigned timeOffset;
};

struct GPointsTuple {
  GPoint point;
  GPoint nextPoint;
  boolean isLast;
};

GVector vectors[NUM_LEDS];

byte mode = 1;

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

    Serial.println();
    Serial.println("IP Address: ");
    Serial.println(WiFi.localIP());

    if (!SPIFFS.begin()) {
        Serial.println("Failed to mount file system");
        return;
    }

    server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");


    server.on("/sosna", HTTP_POST, [](AsyncWebServerRequest *request){
        AsyncWebServerResponse *response = request->beginResponse(
            200,
            "application/json", 
            "{\"result\":{\"device\":\"sosna\",\"leds\":200}}"
        );
        response->addHeader("Access-Control-Allow-Methods","POST, GET, OPTIONS");
        response->addHeader("Access-Control-Allow-Origin","*");
        request->send(response);
    });    

    server.on("/getmodes", HTTP_POST, [](AsyncWebServerRequest *request){
        AsyncWebServerResponse *response = request->beginResponse(
            200,
            "application/json", 
            "{\"result\": {\"mode\":" + String(mode) + ",\"modes\":[{\"id\":1,\"name\":\"s1\"},{\"id\":2,\"name\":\"s2\"},{\"id\":3,\"name\":\"s3\"}]}}"
        );
        response->addHeader("Access-Control-Allow-Methods","POST, GET, OPTIONS");
        response->addHeader("Access-Control-Allow-Origin","*");
        request->send(response);

    });    

    server.on("/setmode", HTTP_POST, [](AsyncWebServerRequest *request){
        AsyncWebParameter* p = request->getParam(0);
        mode =  p->value().toInt();
        AsyncWebServerResponse *response = request->beginResponse(
            200,
            "application/json", 
            "{\"result\": " + String(mode) + "}}"
        );
        response->addHeader("Access-Control-Allow-Methods","POST, GET, OPTIONS");
        response->addHeader("Access-Control-Allow-Origin","*");
        request->send(response);
    });    

    server.on("/clear", HTTP_POST, [](AsyncWebServerRequest *request){
        mode = 0;
        for(int i = 0; i < NUM_LEDS; i++) {
            vectors[i] = (GVector) {(GPoint) {}, 0};
            leds[i] = CRGB(0, 0, 0);            
        }
        AsyncWebServerResponse *response = request->beginResponse(200, "application/json", "{\"result\":\"ok\"}");
        response->addHeader("Access-Control-Allow-Methods","POST, GET, OPTIONS");
        response->addHeader("Access-Control-Allow-Origin","*");
        request->send(response);
    });

    server.on("/set", HTTP_POST, [](AsyncWebServerRequest *request){
        mode = 0;
        String message;
        message = message + "{\"result\":[";
        AsyncWebParameter* p = request->getParam(0);
        if (p->isPost()) {
            String payload = String(p->value().c_str());
            for(int i = 0; i < NUM_LEDS; i++) {
                String ledStr = split(payload, ';', i);
                if (ledStr == "") {
                    break;
                }
                if (i > 0) message = message + ",";
                int led = split(ledStr, ':', 0).toInt();
                long unsigned timeOffset = split(ledStr, ':', 1).toInt();
                
                String pointsStr = split(ledStr, ':', 2);
                
                vectors[led] = (GVector) {{}, timeOffset};

                for(int j = 0; j < 16; j++) {
                    String pointStr = split(pointsStr, '|', j);
                    if (pointStr == "") {
                        break;
                    }
                    uint8_t r = split(pointStr, ',', 0).toInt();
                    uint8_t g = split(pointStr, ',', 1).toInt();
                    uint8_t b = split(pointStr, ',', 2).toInt();
                    long unsigned t = split(pointStr, ',', 3).toInt();
                    byte timeFn = split(pointStr, ',', 4).toInt();
                    byte orderFn = split(pointStr, ',', 4).toInt();

                    vectors[led].points[j] = (GPoint) {t, timeFn, orderFn, (GColor) {r, g, b}};

                }              
                message = message + led;
            }
        }
        message = message + "]}";
        
        AsyncWebServerResponse *response = request->beginResponse(200, "application/json", message);
        response->addHeader("Access-Control-Allow-Methods","POST, GET, OPTIONS");
        response->addHeader("Access-Control-Allow-Origin","*");
        request->send(response);
    });

    server.on("/set", HTTP_OPTIONS, [](AsyncWebServerRequest *request){
        AsyncWebServerResponse *response = request->beginResponse(200, "application/json");
        response->addHeader("Access-Control-Allow-Methods","POST, GET, OPTIONS");
        response->addHeader("Access-Control-Allow-Headers","X-PINGOTHER, Content-Type");
        response->addHeader("Access-Control-Allow-Origin","*");
        response->addHeader("Access-Control-Allow-Private-Network","true");
        request->send(response);
        Serial.println("OPTIONS");
    });

    server.onNotFound(notFound);

    server.begin();

}

void sinus3(long unsigned t) {
    for (int i = 0; i < NUM_LEDS; i++) {
        leds[i] = CRGB(
            (int) (50 + 50 * sin(0.001 * t + 0.000022 * i * t)),
            (int) (50 + 50 * sin(0.001 * t + 0.000021 * i * t)),
            (int) (200 + 55 * sin(0.001 * t + 0.00002 * i * t))
        );
    }
}

void sinus2(long unsigned t) {
    for (int i = 0; i < NUM_LEDS; i++) {
        leds[i] = CRGB(
            (int) (128 + 128 * sin(0.003 * t + 0.05 * i)),
            (int) (128 + 128 * sin(0.0033 * t + 0.051 * i)),
            (int) (128 + 128 * sin(0.0031 * t + 0.052 * i))
        );
    }
}

void sinus1(long unsigned t) {
    for (int i = 0; i < NUM_LEDS; i++) {
        double b = 0.5 + 0.5 * sin(0.003 * t + 99 * i);
        leds[i] = CRGB(
            (int) (b * (230 + 25 * sin(0.001 * t + 0.8 * i))),
            (int) (b * (120 + 25 * sin(0.002 * t + 0.9 * i + PI / 2))),
            (int) (b * (30 + 25 * sin(0.0015 * t + 1.2 * i + PI / 4)))
        );
    }
}

void loop() {
    long unsigned t = millis();
    long unsigned dt = t - lastLedShow;
    if (dt > 10) {
        if (mode == 1) {
            sinus1(t);
        } else if (mode == 2) {
            sinus2(t);
        } else if (mode == 3) {
            sinus3(t);
        } else {
            for (int i = 0; i < NUM_LEDS; i++) {
                GVector vector = vectors[i];
                if (vectors[i].points[0].timeFn > 0) {
                    GPoint last;
                    for (int j = 0; j < 16; j++) {
                        if (vector.points[j].timeFn < 1) {
                            last = vector.points[j - 1];
                            break;
                        }
                    }
                    long unsigned t0 = (last.timeFn > 0 && last.t != 0) ? ((t - vector.timeOffset * i) % last.t) : 0;
                    
                    GPoint next;
                    GPoint prev;
                    for (int j = 0; j < 16; j++) {
                        if (vector.points[j].t > t0) {
                            next = vector.points[j];
                            prev = vector.points[j - 1];
                            break;
                        }
                    }

                    if (next.timeFn > 0 && prev.timeFn > 0) {
                      double k = (next.t != prev.t) ? (((double) (t0 - prev.t)) / ((double) (next.t - prev.t))) : 1;
                      leds[i] = CRGB(
                          (int) (prev.color.r + (next.color.r - prev.color.r) * k),
                          (int) (prev.color.g + (next.color.g - prev.color.g) * k),
                          (int) (prev.color.b + (next.color.b - prev.color.b) * k)
                      );
                    }

                }
            }
        }
        FastLED.show();
        lastLedShow = t;
    }
}

