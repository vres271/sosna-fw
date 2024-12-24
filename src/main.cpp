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
  byte timeFn;
  byte orderFn;
  GColor color;
};

struct GVector {
  GPoint points[16];
  long unsigned t;
};

struct GPointsTuple {
  GPoint point;
  GPoint nextPoint;
  boolean isLast;
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

    Serial.println();
    Serial.println("IP Address: ");
    Serial.println(WiFi.localIP());

    if (!SPIFFS.begin()) {
        Serial.println("Failed to mount file system");
        return;
    }

    server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");

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

            vectors[ledN] = (GVector) {
                {
                    (GPoint) {0, 1, 1, (GColor) {r, g, b}},
                    (GPoint) {50, 1, 1, (GColor) {255, 0, 0}},
                    (GPoint) {100, 1, 1, (GColor) {0, 255, 0}},
                    (GPoint) {150, 1, 1, (GColor) {0, 0, 255}},
                    (GPoint) {200, 1, 1, (GColor) {r, g, b}},
                },
                0
            };
            
            message = "{\"led\": \"" + String(ledN) + "\"}";
        } else {
            message = "{\"error\": \"Empty params\"}";
        }
        AsyncWebServerResponse *response = request->beginResponse(200, "application/json", message);
        response->addHeader("Access-Control-Allow-Methods","POST, GET, OPTIONS");
        response->addHeader("Access-Control-Allow-Origin","*");
        request->send(response);
        Serial.println(message);
    });

    server.on("/clear", HTTP_POST, [](AsyncWebServerRequest *request){
        for(int i = 0; i < NUM_LEDS; i++) {
            vectors[i] = (GVector) {(GPoint) {}, 0};
            leds[i] = CRGB(0, 0, 0);            
        }
        AsyncWebServerResponse *response = request->beginResponse(200, "application/json", "{\"result\": \"ok\"}");
        response->addHeader("Access-Control-Allow-Methods","POST, GET, OPTIONS");
        response->addHeader("Access-Control-Allow-Origin","*");
        request->send(response);
    });

    server.on("/set", HTTP_POST, [](AsyncWebServerRequest *request){
        String message;
        // Serial.println("POST");
        // int params = request->params();
        // for(int i=0;i<params;i++){
        //     AsyncWebParameter* p = request->getParam(i);
        //     if(p->isFile()){ //p->isPost() is also true
        //         Serial.printf("FILE[%s]: %s, size: %u\n", p->name().c_str(), p->value().c_str(), p->size());
        //     } else if(p->isPost()){
        //         message = String(p->name().c_str()) + " " +  String(p->value().c_str());
        //         Serial.printf("POST[%s]: %s\n", p->name().c_str(), p->value().c_str());
        //     } else {
        //         Serial.printf("GET[%s]: %s\n", p->name().c_str(), p->value().c_str());
        //     }
        // }

        message = message + "{\"result\": [";
        AsyncWebParameter* p = request->getParam(0);
        if (p->isPost()) {
            String payload = String(p->value().c_str());
            // Serial.println("payload: " + payload);
            for(int i = 0; i < NUM_LEDS; i++) {
                String ledStr = split(payload, ';', i);
                if (ledStr == "") {
                    break;
                }
                if (i > 0) message = message + ",";
                // Serial.println("ledStr: " + ledStr);
                int led = split(ledStr, ':', 0).toInt();
                
                String pointsStr = split(ledStr, ':', 1);
                // Serial.println("pointsStr: " + pointsStr);
                
                vectors[led] = (GVector) {{}, 0};

                for(int j = 0; j < 16; j++) {
                    String pointStr = split(pointsStr, '|', j);
                    if (pointStr == "") {
                        break;
                    }
                    // Serial.println("pointStr: " + pointStr);
                    uint8_t r = split(pointStr, ',', 0).toInt();
                    uint8_t g = split(pointStr, ',', 1).toInt();
                    uint8_t b = split(pointStr, ',', 2).toInt();
                    long unsigned t = split(pointStr, ',', 3).toInt();
                    byte timeFn = split(pointStr, ',', 4).toInt();
                    byte orderFn = split(pointStr, ',', 4).toInt();

                    vectors[led].points[j] = (GPoint) {t, timeFn, orderFn, (GColor) {r, g, b}};

                    // Serial.println(
                    //     "r: " + String(r) + ", "
                    //     "g: " + String(g) + ", "
                    //     "b: " + String(b) + ", "
                    //     "t: " + String(t) + ", "
                    //     "timeFn: " + String(timeFn) + ", "
                    //     "orderFn: "  + String(orderFn)
                    // );
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


GPointsTuple getCurrentPoint(GVector vector) {

    GPoint firstPoint = vector.points[0];
    GPoint lastPoint;

    for (int j = 0; j < 16; j++) {
        if (vector.points[j+1].orderFn == 0) {
            lastPoint = vector.points[j];
            break;
        }
    }
    for (int j = 0; j < 16; j++) {
        if (vector.points[j].t <= vector.t && vector.t < vector.points[j+1].t) {
            return {vector.points[j], vector.points[j+1], false};
        }
        if (vector.points[j].orderFn == 0) {
            break;
        }
    }
    // Serial.println("lastPoint: " + String(lastPoint.t) + "firstPoint: " + String(firstPoint.t));

    return {lastPoint, firstPoint, vector.t > lastPoint.t};

}

void loop() {
    long unsigned t = millis();
    long unsigned dt = t - lastLedShow;
    if (dt > 30) {
        // Serial.println("t: " + String(t) + " --------------");
        for (int i = 0; i < NUM_LEDS; i++) {
            GVector vector = vectors[i];
            if (vectors[i].points[0].timeFn > 0) {
                vectors[i].t = vectors[i].t + dt;

                // Serial.println(" led:" + String(i));
                GPointsTuple tuple = getCurrentPoint(vector);

                // Serial.print("" + String(vector.t) + " => [" + String(tuple.point.t) + ", " + String(tuple.nextPoint.t) + "]; ");
                leds[i] = CRGB(
                    tuple.point.color.r,
                    tuple.point.color.g,
                    tuple.point.color.b
                );
                if (tuple.isLast) {
                    // Serial.print(" reset t;");
                    vectors[i].t = 0;
                }
                // Serial.println();
            }
        }
        FastLED.show();
        lastLedShow = t;
    }
}

