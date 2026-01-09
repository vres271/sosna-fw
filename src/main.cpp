#include <Arduino.h>
#include <utils.cpp>

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

#define WIFI_SSID "TP-Link_49CB"
#define WIFI_PASS "sukaher271"

#include <FastLED.h>

#define NUM_LEDS 200
#define LED_PIN 5
#define LED_TYPE WS2812B
#define LED_ORDER GRB

CRGB leds[NUM_LEDS];
byte mode = 1;
long unsigned lastLedShow = 0;

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

GVector vectors[NUM_LEDS];

void notFound(AsyncWebServerRequest *request) {
    request->send(404, "text/plain", "Not found");
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    FastLED.addLeds<LED_TYPE, LED_PIN, LED_ORDER>(leds, NUM_LEDS).setCorrection(0xFF80F0);
    FastLED.setBrightness(255);
    
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi Failed!");
        return;
    }
    
    Serial.println();
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());

    if (!SPIFFS.begin()) {
        Serial.println("SPIFFS Failed!");
        return;
    }

    server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");

    // ===== SOSNA =====
    server.on("/sosna", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(200, "application/json", "{\"result\":{\"device\":\"sosna\",\"leds\":200}}");
    });
    
    server.on("/sosna", HTTP_POST, [](AsyncWebServerRequest *request){
        request->send(200, "application/json", "{\"result\":{\"device\":\"sosna\",\"leds\":200}}");
    });

    // ===== GET MODES =====
    server.on("/getmodes", HTTP_POST, [](AsyncWebServerRequest *request){
        String json = "{\"result\": {\"mode\":" + String(mode) + ",\"modes\":[";
        json += "{\"id\":1,\"name\":\"sinus1\"},{\"id\":2,\"name\":\"sinus2\"},{\"id\":3,\"name\":\"sinus3\"},";
        json += "{\"id\":4,\"name\":\"purpleWave\"},{\"id\":5,\"name\":\"cyanRush\"},{\"id\":6,\"name\":\"sunsetGlow\"},";
        json += "{\"id\":7,\"name\":\"softPink\"},{\"id\":8,\"name\":\"emeraldGreen\"},{\"id\":9,\"name\":\"rainbowFrenzy\"},";
        json += "{\"id\":10,\"name\":\"lavenderDawn\"},{\"id\":11,\"name\":\"icyBlue\"},{\"id\":12,\"name\":\"magicalMagenta\"},";
        json += "{\"id\":13,\"name\":\"neonShock\"},{\"id\":14,\"name\":\"forestMoss\"},{\"id\":15,\"name\":\"flowerRomance\"}";
        json += "]}}";
        request->send(200, "application/json", json);
    });

    // ===== SET MODE (НЕБЛОКИРУЮЩИЙ) =====
    server.on("/setmode", HTTP_POST, [](AsyncWebServerRequest *request){
        if (request->hasParam("mode", true)) {
            mode = request->getParam("mode", true)->value().toInt();
            request->send(200, "application/json", "{\"result\":" + String(mode) + "}");
        } else {
            request->send(400, "application/json", "{\"error\":\"missing mode parameter\"}");
        }
    });

    // ===== CLEAR =====
    server.on("/clear", HTTP_POST, [](AsyncWebServerRequest *request){
        mode = 0;
        for(int i = 0; i < NUM_LEDS; i++) {
            vectors[i] = (GVector) {(GPoint) {}, 0};
            leds[i] = CRGB(0, 0, 0);
        }
        FastLED.show();
        request->send(200, "application/json", "{\"result\":\"ok\"}");
    });

    // ===== SET (ОПТИМИЗИРОВАННЫЙ) =====
    server.on("/set", HTTP_POST, [](AsyncWebServerRequest *request){
        mode = 0;
        
        // Парсинг только необходимого объема данных
        if (request->hasParam("data", true)) {
            String payload = request->getParam("data", true)->value();
            
            // Ограничение размера payload'а
            if (payload.length() > 8000) {
                request->send(400, "application/json", "{\"error\":\"payload too large\"}");
                return;
            }
            
            int processedLeds = 0;
            int i = 0;
            
            // Неблокирующий парсинг
            while (i < payload.length() && processedLeds < NUM_LEDS) {
                int semiPos = payload.indexOf(';', i);
                if (semiPos == -1) semiPos = payload.length();
                
                String ledStr = payload.substring(i, semiPos);
                
                if (ledStr.length() > 0) {
                    int firstColon = ledStr.indexOf(':');
                    int secondColon = ledStr.indexOf(':', firstColon + 1);
                    
                    if (firstColon > 0 && secondColon > firstColon) {
                        int led = ledStr.substring(0, firstColon).toInt();
                        long unsigned timeOffset = ledStr.substring(firstColon + 1, secondColon).toInt();
                        String pointsStr = ledStr.substring(secondColon + 1);
                        
                        if (led >= 0 && led < NUM_LEDS) {
                            vectors[led] = (GVector) {{}, timeOffset};
                            
                            int pointIdx = 0;
                            int pointStart = 0;
                            
                            while (pointIdx < 16) {
                                int pipePos = pointsStr.indexOf('|', pointStart);
                                if (pipePos == -1) pipePos = pointsStr.length();
                                
                                String pointStr = pointsStr.substring(pointStart, pipePos);
                                
                                if (pointStr.length() > 0) {
                                    // Парсинг точки
                                    int commaPos[5] = {-1, -1, -1, -1, -1};
                                    int commaIdx = 0;
                                    
                                    for (int j = 0; j < pointStr.length() && commaIdx < 5; j++) {
                                        if (pointStr[j] == ',') {
                                            commaPos[commaIdx++] = j;
                                        }
                                    }
                                    
                                    if (commaIdx >= 4) {
                                        uint8_t r = pointStr.substring(0, commaPos[0]).toInt();
                                        uint8_t g = pointStr.substring(commaPos[0] + 1, commaPos[1]).toInt();
                                        uint8_t b = pointStr.substring(commaPos[1] + 1, commaPos[2]).toInt();
                                        long unsigned t = pointStr.substring(commaPos[2] + 1, commaPos[3]).toInt();
                                        byte timeFn = pointStr.substring(commaPos[3] + 1).toInt();
                                        
                                        vectors[led].points[pointIdx] = (GPoint) {t, timeFn, timeFn, (GColor) {r, g, b}};
                                        pointIdx++;
                                    }
                                } else {
                                    break;
                                }
                                
                                pointStart = pipePos + 1;
                            }
                            
                            processedLeds++;
                        }
                    }
                }
                
                i = semiPos + 1;
            }
            
            request->send(200, "application/json", "{\"result\":{\"processed\":" + String(processedLeds) + "}}");
        } else {
            request->send(400, "application/json", "{\"error\":\"missing data parameter\"}");
        }
    });

    // ===== OPTIONS (CORS) =====
    server.on("/set", HTTP_OPTIONS, [](AsyncWebServerRequest *request){
        AsyncWebServerResponse *response = request->beginResponse(200);
        response->addHeader("Access-Control-Allow-Methods", "POST, GET, OPTIONS");
        response->addHeader("Access-Control-Allow-Headers", "Content-Type");
        response->addHeader("Access-Control-Allow-Origin", "*");
        request->send(response);
    });
    
    server.on("/sosna", HTTP_OPTIONS, [](AsyncWebServerRequest *request){
        AsyncWebServerResponse *response = request->beginResponse(200);
        response->addHeader("Access-Control-Allow-Methods", "POST, GET, OPTIONS");
        response->addHeader("Access-Control-Allow-Origin", "*");
        request->send(response);
    });
    
    server.on("/setmode", HTTP_OPTIONS, [](AsyncWebServerRequest *request){
        AsyncWebServerResponse *response = request->beginResponse(200);
        response->addHeader("Access-Control-Allow-Methods", "POST, GET, OPTIONS");
        response->addHeader("Access-Control-Allow-Origin", "*");
        request->send(response);
    });

    server.onNotFound(notFound);
    server.begin();
}

// ============ РЕЖИМЫ ============

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

void sinus2(long unsigned t) {
    for (int i = 0; i < NUM_LEDS; i++) {
        leds[i] = CRGB(
            (int) (128 + 128 * sin(0.003 * t + 0.05 * i)),
            (int) (128 + 128 * sin(0.0033 * t + 0.051 * i)),
            (int) (128 + 128 * sin(0.0031 * t + 0.052 * i))
        );
    }
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

void purpleWave(long unsigned t) {
    for (int i = 0; i < NUM_LEDS; i++) {
        double wave = 0.5 + 0.5 * sin(0.002 * t + 0.08 * i);
        leds[i] = CRGB(
            (int) (80 + 120 * sin(0.0025 * t + 0.7 * i)),
            (int) (30 + 40 * sin(0.002 * t + 0.75 * i + PI / 3)),
            (int) (200 + 55 * wave * sin(0.003 * t + 0.85 * i))
        );
    }
}

void cyanRush(long unsigned t) {
    for (int i = 0; i < NUM_LEDS; i++) {
        double phase = 0.006 * t + 0.15 * i;
        leds[i] = CRGB(
            (int) (40 + 60 * sin(phase)),
            (int) (180 + 75 * sin(phase + PI / 3)),
            (int) (200 + 55 * sin(phase + PI / 6))
        );
    }
}

void sunsetGlow(long unsigned t) {
    for (int i = 0; i < NUM_LEDS; i++) {
        double breathing = 0.5 + 0.5 * sin(0.0015 * t + 0.03 * i);
        leds[i] = CRGB(
            (int) (200 + 55 * breathing * sin(0.002 * t + 0.6 * i)),
            (int) (100 + 80 * breathing * sin(0.0018 * t + 0.65 * i)),
            (int) (30 + 20 * sin(0.0017 * t + 0.7 * i))
        );
    }
}

void softPink(long unsigned t) {
    for (int i = 0; i < NUM_LEDS; i++) {
        double slowWave = 0.4 + 0.6 * sin(0.001 * t + 0.04 * i);
        leds[i] = CRGB(
            (int) (200 + 55 * slowWave),
            (int) (120 + 60 * sin(0.0015 * t + 0.5 * i)),
            (int) (150 + 70 * sin(0.0014 * t + 0.55 * i))
        );
    }
}

void emeraldGreen(long unsigned t) {
    for (int i = 0; i < NUM_LEDS; i++) {
        double mainWave = 0.5 + 0.5 * sin(0.004 * t + 0.1 * i);
        leds[i] = CRGB(
            (int) (30 + 40 * sin(0.003 * t + 0.75 * i)),
            (int) (180 + 75 * mainWave * sin(0.0035 * t + 0.8 * i)),
            (int) (100 + 70 * sin(0.0032 * t + 0.85 * i))
        );
    }
}

void rainbowFrenzy(long unsigned t) {
    for (int i = 0; i < NUM_LEDS; i++) {
        leds[i] = CRGB(
            (int) (128 + 127 * sin(0.005 * t + 0.2 * i)),
            (int) (128 + 127 * sin(0.0048 * t + 0.21 * i + PI / 2)),
            (int) (128 + 127 * sin(0.0052 * t + 0.22 * i + PI))
        );
    }
}

void lavenderDawn(long unsigned t) {
    for (int i = 0; i < NUM_LEDS; i++) {
        double gentle = 0.6 + 0.4 * sin(0.0008 * t + 0.02 * i);
        leds[i] = CRGB(
            (int) (160 + 50 * gentle * sin(0.001 * t + 0.4 * i)),
            (int) (110 + 45 * gentle * sin(0.0011 * t + 0.42 * i)),
            (int) (180 + 60 * gentle * sin(0.0009 * t + 0.38 * i))
        );
    }
}

void icyBlue(long unsigned t) {
    for (int i = 0; i < NUM_LEDS; i++) {
        double shimmer = 0.5 + 0.5 * sin(0.0035 * t + 0.12 * i);
        leds[i] = CRGB(
            (int) (50 + 50 * sin(0.002 * t + 0.65 * i)),
            (int) (120 + 80 * shimmer * sin(0.003 * t + 0.7 * i)),
            (int) (220 + 35 * sin(0.0032 * t + 0.75 * i))
        );
    }
}

void magicalMagenta(long unsigned t) {
    for (int i = 0; i < NUM_LEDS; i++) {
        double pulse = 0.4 + 0.6 * sin(0.0022 * t + 0.09 * i);
        leds[i] = CRGB(
            (int) (180 + 75 * pulse * sin(0.0025 * t + 0.6 * i)),
            (int) (60 + 50 * sin(0.0023 * t + 0.65 * i)),
            (int) (200 + 55 * pulse * sin(0.0024 * t + 0.58 * i))
        );
    }
}

void neonShock(long unsigned t) {
    for (int i = 0; i < NUM_LEDS; i++) {
        leds[i] = CRGB(
            (int) (100 + 155 * sin(0.0055 * t + 0.18 * i)),
            (int) (200 + 55 * sin(0.0058 * t + 0.19 * i + PI / 4)),
            (int) (150 + 105 * sin(0.0052 * t + 0.17 * i + PI / 2))
        );
    }
}

void forestMoss(long unsigned t) {
    for (int i = 0; i < NUM_LEDS; i++) {
        double breathe = 0.55 + 0.45 * sin(0.0012 * t + 0.025 * i);
        leds[i] = CRGB(
            (int) (60 + 50 * sin(0.0013 * t + 0.5 * i)),
            (int) (140 + 80 * breathe * sin(0.0014 * t + 0.52 * i)),
            (int) (80 + 70 * sin(0.00125 * t + 0.48 * i))
        );
    }
}

void flowerRomance(long unsigned t) {
    for (int i = 0; i < NUM_LEDS; i++) {
        double wave = 0.5 + 0.5 * sin(0.0018 * t + 0.06 * i);
        leds[i] = CRGB(
            (int) (210 + 45 * wave * sin(0.002 * t + 0.55 * i)),
            (int) (100 + 60 * sin(0.0019 * t + 0.58 * i + PI/3)),
            (int) (170 + 70 * wave * sin(0.00175 * t + 0.52 * i))
        );
    }
}

// ============ LOOP ============

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
        } else if (mode == 4) {
            purpleWave(t);
        } else if (mode == 5) {
            cyanRush(t);
        } else if (mode == 6) {
            sunsetGlow(t);
        } else if (mode == 7) {
            softPink(t);
        } else if (mode == 8) {
            emeraldGreen(t);
        } else if (mode == 9) {
            rainbowFrenzy(t);
        } else if (mode == 10) {
            lavenderDawn(t);
        } else if (mode == 11) {
            icyBlue(t);
        } else if (mode == 12) {
            magicalMagenta(t);
        } else if (mode == 13) {
            neonShock(t);
        } else if (mode == 14) {
            forestMoss(t);
        } else if (mode == 15) {
            flowerRomance(t);
        } else {
            // Vector animation mode
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
    
    yield(); // Даём WiFi и AsyncWebServer обработать запросы
}
