#include <SoftwareSerial.h>
#include <ArduinoJson.h>
// 基础配置
#define BUFFER_SIZE 512
#define CLIENT_ID "4G_GPS"              // 设备唯一标识

// MQTT主题定义
#define TOPIC_SUB_AT "AT_GPS"           // 订阅：接收AT指令
#define TOPIC_PUB_GPS "4G"              // 发布：GPS位置数据
#define TOPIC_PUB_RESULT "AT_GPS_result"// 发布：AT指令执行结果

// 时间常量定义
const unsigned long CYCLE_TOTAL = 10000;    // 修改为10秒完整周期
const unsigned long NMEA_ACTIVE = 2000;     // NMEA激活时间2秒
const unsigned long MQTT_SEND_INTERVAL = 3000; // MQTT发送间隔3秒

SoftwareSerial SerialAT(5, 4); // RX=GPIO5, TX=GPIO4
bool sendATCommand(const char* command, String* response = nullptr, int timeout = 1000) {
  // 打印发送的AT指令
  Serial.printf("发送AT指令: %s\n", command);
  SerialAT.println(command);
  
  if (response != nullptr) {
    response->clear();
    unsigned long start = millis();
    while (millis() - start < timeout) {
      if (SerialAT.available()) {
        char c = SerialAT.read();
        *response += c;
        Serial.write(c);  // 实时显示接收到的数据
      }
    }
  } else {
    delay(timeout);
    while (SerialAT.available()) {
      Serial.write(SerialAT.read());  // 直接输出调试信息
    }
  }
  
  return true;
}

void setup() {
  // 初始化调试串口(USB)
  Serial.begin(115200);
  // 初始化4G模块串口 (RX=16, TX=17)
  SerialAT.begin(115200);
  
  Serial.println("正在初始化模块...");
  delay(10000);  // 等待模块启动

  // 测试AT指令响应
  int retryCount = 0;
  const int maxRetries = 10;
  bool moduleReady = false;

  while (retryCount < maxRetries) {
    Serial.printf("\n第 %d 次尝试连接模块...\n", retryCount + 1);
    String response;
    
    Serial.println("发送AT指令...");
    if (sendATCommand("AT", &response, 2000)) {
      Serial.printf("接收到响应: %s\n", response.c_str());
      if (response.indexOf("OK") != -1) {
        moduleReady = true;
        Serial.println("模块响应正常，初始化成功！");
        break;
      }
    }
    retryCount++;
    delay(2000);
  }

  if (!moduleReady) {
    Serial.println("模块未就绪，请检查连接");
    while(1) delay(1000);  // 停止执行
  }

  // 初始化配置
  initModem();
}

void initModem() {
  for (int i = 0; i < 1; i++)//大力出奇迹，尝试三次。
  {
  Serial.println("发送AT指令，设置GPRS参数");
  sendATCommand("AT+QICSGP=1,1,\"\",\"\",\"\"", nullptr, 1000);

  Serial.println("打开GPRS连接");
  sendATCommand("AT+NETOPEN", nullptr, 2000);

  Serial.println("设置NTP服务器");
  sendATCommand("AT+QNTP=1,\"tms.dynamicode.com.cn\",123,1", nullptr, 500);

  Serial.println("设置MCONFIG参数");
  sendATCommand("AT+MCONFIG=\"4G_GPS\",\"4G_GPS\",  mqtt客户端密码  ", nullptr, 500);

  Serial.println("设置MIPSTART参数");
  sendATCommand("AT+MIPSTART=\"  mqtt服务器地址  \",1883", nullptr, 500);

  Serial.println("连接MQTT服务器");
  sendATCommand("AT+MCONNECT=1,60", nullptr, 1000);

  Serial.println("订阅主题");
  sendATCommand("AT+MSUB=\"" TOPIC_SUB_AT "\",0", nullptr, 1000);
  
  // 添加新的发布主题
  Serial.println("添加结果反馈主题");
  sendATCommand("AT+MPUB=\"" TOPIC_PUB_RESULT "\",0,0,\"初始化\"", nullptr, 1000);
  

  Serial.println("开启GPS");
  sendATCommand("AT+MGPSC=1", nullptr, 1000);

  Serial.println("设置热启动");
  sendATCommand("AT+GPSMODE=1", nullptr, 1000);

  Serial.println("发布消息：等待获取GPS信息");
  sendATCommand("AT+MPUB=\"" TOPIC_PUB_GPS "\",0,0,\"等待获取GPS信息\"", nullptr, 1000);

  
  }
  sendATCommand("AT+MPUB=\"" TOPIC_PUB_RESULT "\",0,0,\"初始化完成\"", nullptr, 1000);

  Serial.println("开启NMEA消息");
  sendATCommand("AT+MGPSGET=ALL,1", nullptr, 1000);

}

// 修改处理订阅消息的函数
void handleATCommand(const char* atCommand) {
  Serial.printf("开始执行AT命令: %s\n", atCommand);

  String response;
  if (sendATCommand(atCommand, &response, 3000)) {
    Serial.printf("AT命令响应内容: %s\n", response.c_str());

    // 创建JSON文档
    StaticJsonDocument<BUFFER_SIZE> doc;
    doc["status"] = response.indexOf("ERROR") == -1;

    // 将响应拆分成行
    String line;
    int lineCount = 0;
    int pos = 0;
    while (pos < response.length()) {
      int endPos = response.indexOf('\n', pos);
      if (endPos == -1) {
        line = response.substring(pos);
        pos = response.length();
      } else {
        line = response.substring(pos, endPos);
        pos = endPos + 1;
      }
      
      // 清理行末的\r
      if (line.endsWith("\r")) {
        line = line.substring(0, line.length() - 1);
      }
      
      // 跳过空行
      if (line.length() > 0) {
        String key = "line" + String(lineCount + 1);
        doc[key] = line;
        lineCount++;
      }
    }

    // 生成JSON字符串
    String jsonString;
    serializeJson(doc, jsonString);

    // 替换特殊字符（保留对双引号的转义）
    jsonString.replace("\n", "");
    jsonString.replace("\r", "");

    // 构造和发送MQTT消息
    char resultMsg[BUFFER_SIZE];
    snprintf(resultMsg, BUFFER_SIZE, "AT+MPUB=\"%s\",0,0,\"%s\"", TOPIC_PUB_RESULT, jsonString.c_str());

    Serial.println("发送MQTT结果消息...");
    String pubResponse;
    if (sendATCommand(resultMsg, &pubResponse, 2000)) {
      if (pubResponse.indexOf("ERROR") == -1) {
        Serial.println("MQTT结果发送成功！");
      } else {
        Serial.println("MQTT结果发送失败！");
      }
    } else {
      Serial.println("MQTT结果发送失败！");
    }
  } else {
    Serial.println("AT命令执行失败！");
  }

  Serial.println("------------------------");
  delay(1000);
}

void extractGPSData(const char* response, char* gpsData) {
  const char* start = strstr(response, "+GPSST:");
  if (start) {
    start += 7; // 跳过"+GPSST:"
    const char* end = strstr(start, "\r\n");
    if (end) {
      int len = end - start;
      strncpy(gpsData, start, len);
      gpsData[len] = '\0';
    } else {
      strcpy(gpsData, start);
    }
  } else {
    gpsData[0] = '\0';
  }
}

void parseGPSToJSON(const char* gpsData, char* jsonOutput) {
  int status, cn;
  double longitude, altitude, latitude;
  
  sscanf(gpsData, "%d, %d, %lf, %lf, %lf", &status, &cn, &longitude, &altitude, &latitude);
  
  sprintf(jsonOutput, 
          "{\"status\":%d,\"cn\":%d,\"wgs84\":{\"longitude\":%.6f,\"latitude\":%.6f,\"altitude\":%.6f}}",
          status, cn, longitude, latitude, altitude);
}

// 解析NMEA中$GNGGA语句生成JSON
void parseNMEAGGAToJSON(const char* nmea, char* jsonOutput) {
  char nmeaCopy[BUFFER_SIZE];
  strncpy(nmeaCopy, nmea, BUFFER_SIZE-1);
  nmeaCopy[BUFFER_SIZE-1] = '\0';
  char *token = strtok(nmeaCopy, ",");
  if (!token || strcmp(token, "$GNGGA") != 0) {
    strcpy(jsonOutput, "{\"status\":0}");
    return;
  }
  // 字段顺序：$GNGGA,UTC,lat,NS,lon,EW,fix,cn,hdop,alt,altUnit,...
  strtok(NULL, ","); // UTC 忽略
  char *latStr = strtok(NULL, ",");    // 纬度 (格式：ddmm.mmmmmm)
  char *ns = strtok(NULL, ",");          // N/S
  char *lonStr = strtok(NULL, ",");      // 经度 (格式：dddmm.mmmmmm)
  char *ew = strtok(NULL, ",");          // E/W
  char *fixQuality = strtok(NULL, ",");  // 定位状态
  char *cnStr = strtok(NULL, ",");       // 卫星数量
  strtok(NULL, ","); // hdop 忽略
  char *altStr = strtok(NULL, ",");      // 海拔
  int fix = atoi(fixQuality);
  int cn = atoi(cnStr);
  double altitude = atof(altStr);
  double latDegrees = 0, lonDegrees = 0;
  if(latStr && strlen(latStr) >= 4) {
    char degPart[3] = {0};
    strncpy(degPart, latStr, 2);
    double deg = atof(degPart);
    double min = atof(latStr + 2);
    latDegrees = deg + min/60.0;
    if(ns && (ns[0]=='S' || ns[0]=='s'))
      latDegrees = -latDegrees;
  }
  if(lonStr && strlen(lonStr) >= 5) {
    char degPart[4] = {0};
    strncpy(degPart, lonStr, 3);
    double deg = atof(degPart);
    double min = atof(lonStr + 3);
    lonDegrees = deg + min/60.0;
    if(ew && (ew[0]=='W' || ew[0]=='w'))
      lonDegrees = -lonDegrees;
  }
  int status = (fix > 0) ? 1 : 0;
  sprintf(jsonOutput,
          "{\"status\":%d,\"cn\":1,\"wgs84\":{\"longitude\":%.6f,\"latitude\":%.6f,\"altitude\":%.2f}}",
          status, lonDegrees, latDegrees, altitude);
}

// 新增函数：解析 NMEA $GNRMC 语句生成JSON
void parseNMEARMCToJSON(const char* nmea, char* jsonOutput) {
  char nmeaCopy[BUFFER_SIZE];
  strncpy(nmeaCopy, nmea, BUFFER_SIZE - 1);
  nmeaCopy[BUFFER_SIZE - 1] = '\0';
  
  // 使用 strtok 按逗号分割
  char *token = strtok(nmeaCopy, ",");
  if (!token || strcmp(token, "$GNRMC") != 0) {
    strcpy(jsonOutput, "{\"status\":0}");
    return;
  }
  // $GNRMC,UTC,time,status,lat,NS,lon,EW,...
  strtok(NULL, ","); // UTC时间，忽略
  char *statusStr = strtok(NULL, ","); // 状态 A=有效 V=无效
  char *latStr = strtok(NULL, ",");      // 纬度 ddmm.mmmmmm
  char *ns = strtok(NULL, ",");            // N/S
  char *lonStr = strtok(NULL, ",");      // 经度 dddmm.mmmmmm
  char *ew = strtok(NULL, ",");            // E/W
  
  int valid = (statusStr && statusStr[0] == 'A') ? 1 : 0;
  double latDegrees = 0, lonDegrees = 0;
  if(latStr && strlen(latStr) >= 4) {
    char degPart[3] = {0};
    strncpy(degPart, latStr, 2);
    double deg = atof(degPart);
    double min = atof(latStr + 2);
    latDegrees = deg + min/60.0;
    if(ns && (ns[0]=='S' || ns[0]=='s'))
      latDegrees = -latDegrees;
  }
  if(lonStr && strlen(lonStr) >= 5) {
    char degPart[4] = {0};
    strncpy(degPart, lonStr, 3);
    double deg = atof(degPart);
    double min = atof(lonStr + 3);
    lonDegrees = deg + min/60.0;
    if(ew && (ew[0]=='W' || ew[0]=='w'))
      lonDegrees = -lonDegrees;
  }
  
  sprintf(jsonOutput,
          "{\"status\":%d,\"cn\":1,\"wgs84\":{\"longitude\":%.6f,\"latitude\":%.6f,\"altitude\":0.00}}",
          valid, lonDegrees, latDegrees);
}

// 解析AT指令JSON
bool parseATCommandJSON(const char* jsonString, String& atCommand) {
  Serial.println("开始解析JSON...");
  Serial.printf("输入JSON字符串: '%s'\n", jsonString);

  // 创建一个静态的 JSON 文档
  StaticJsonDocument<BUFFER_SIZE> doc;
  DeserializationError error = deserializeJson(doc, jsonString);

  if (error) {
    Serial.print(F("JSON解析失败: "));
    Serial.println(error.f_str());
    return false;
  }

  // 提取 client_id 和 at_cmd
  const char* clientId = doc["client_id"];
  const char* cmd = doc["at_cmd"];

  if (!clientId || !cmd) {
    Serial.println("未找到必要的JSON字段");
    return false;
  }

  Serial.printf("提取的ID: '%s'\n", clientId);

  if (String(clientId) != CLIENT_ID) {
    Serial.printf("ID不匹配。收到:'%s' 期望:'%s'\n", clientId, CLIENT_ID);
    return false;
  }

  atCommand = String(cmd);
  Serial.printf("成功提取命令: '%s'\n", atCommand.c_str());
  return true;
}

// 添加串口缓冲区，用于接收完整消息
String serialBuffer = "";

// 在全局变量区域添加
bool isCollectingMessage = false;
String fullMessage = "";

// 添加一个新的辅助函数用于清理JSON字符串
String cleanJsonString(const String& input) {
  // 使用 ArduinoJson 库不需要清理 JSON 字符串
  return input;
}

// 添加全局变量用于存储上一次的坐标
struct LastCoordinates {
    double longitude;
    double latitude;
    double altitude;
    bool isValid;
} lastCoords = {0, 0, 0, false};

// 修改阈值为更小的值，以保持原有精度
bool hasLocationChanged(double newLon, double newLat, double newAlt) {
    if (!lastCoords.isValid) {
        return true;
    }
    // 使用更小的阈值，确保不影响原有精度
    const double threshold = 0.0000001;  // 精确到0.0000001度
    return fabs(newLon - lastCoords.longitude) > threshold ||
           fabs(newLat - lastCoords.latitude) > threshold ||
           fabs(newAlt - lastCoords.altitude) > threshold;
}

// 在全局区域添加消息队列相关定义
#define MAX_QUEUE_SIZE 2  // 减小队列大小
struct GPSMessage {
    char jsonData[96];    // 减小单条消息的缓冲区
    bool used;
};
GPSMessage gpsQueue[MAX_QUEUE_SIZE];
uint8_t queueHead = 0;
uint8_t queueTail = 0;
unsigned long lastMQTTSend = 0;

// 添加队列操作函数
bool enqueueGPS(const char* jsonData) {
    uint8_t nextTail = (queueTail + 1) % MAX_QUEUE_SIZE;
    if (nextTail != queueHead) {
        strncpy(gpsQueue[queueTail].jsonData, jsonData, sizeof(gpsQueue[queueTail].jsonData) - 1);
        gpsQueue[queueTail].used = true;
        queueTail = nextTail;
        return true;
    }
    return false;
}

bool dequeueGPS(char* jsonData) {
    if (queueHead != queueTail && gpsQueue[queueHead].used) {
        strncpy(jsonData, gpsQueue[queueHead].jsonData, 96);
        gpsQueue[queueHead].used = false;
        queueHead = (queueHead + 1) % MAX_QUEUE_SIZE;
        return true;
    }
    return false;
}

// 修改 processMQTTMessage 函数
unsigned long lastNMEAOpen = 0;
unsigned long lastNMEAProcessTime = 0;

// 修改为静态变量以存储消息状态
static struct {
    bool inJsonMessage;
    bool inNMEAMessage;
    String buffer;
} msgState = {false, false, ""};

void processMQTTMessage(const String& message) {
    Serial.printf("处理新消息: %s\n", message.c_str());
    
    // 检查是否为MQTT订阅消息开始
    if (message.indexOf("+MSUB: \"" TOPIC_SUB_AT "\"") != -1) {
        msgState.inJsonMessage = true;
        msgState.inNMEAMessage = false;
        msgState.buffer = "";
        return;
    }
    
    // 检查是否为NMEA消息
    if (message.startsWith("$GN")) {
        // 如果之前在处理JSON消息，清空状态
        if (msgState.inJsonMessage) {
            msgState.inJsonMessage = false;
            msgState.buffer = "";
        }
        msgState.inNMEAMessage = true;
        processNMEAMessage(message);
        return;
    }
    
    // 处理JSON消息累积
    if (msgState.inJsonMessage) {
        msgState.buffer += message;
        
        // 检查是否有完整的JSON
        int startPos = msgState.buffer.indexOf("{");
        int endPos = msgState.buffer.indexOf("}");
        
        if (startPos != -1 && endPos != -1) {
            String jsonStr = msgState.buffer.substring(startPos, endPos + 1);
            String atCommand;
            if (parseATCommandJSON(jsonStr.c_str(), atCommand)) {
                handleATCommand(atCommand.c_str());
            }
            msgState.inJsonMessage = false;
            msgState.buffer = "";
        }
    }
}

// 新增NMEA消息处理函数
void processNMEAMessage(const String& message) {
    // 仅处理完整的NMEA消息
    if (message.indexOf("*") == -1 || 
        !(message.startsWith("$GNRMC") || message.startsWith("$GNGGA"))) {
        return;
    }

    if (millis() - lastNMEAProcessTime < 2000) {
        return;
    }

    char jsonData[96] = {0};
    if (message.startsWith("$GNRMC")) {
        parseNMEARMCToJSON(message.c_str(), jsonData);
    } else {
        parseNMEAGGAToJSON(message.c_str(), jsonData);
    }

    StaticJsonDocument<96> doc;
    DeserializationError error = deserializeJson(doc, jsonData);
    
    if (!error && doc["status"] == 1) {
        double newLon = doc["wgs84"]["longitude"];
        double newLat = doc["wgs84"]["latitude"];
        double newAlt = doc["wgs84"]["altitude"];

        if (hasLocationChanged(newLon, newLat, newAlt)) {
            enqueueGPS(jsonData);
            lastCoords.longitude = newLon;
            lastCoords.latitude = newLat;
            lastCoords.altitude = newAlt;
            lastCoords.isValid = true;
        }
    }
    
    lastNMEAProcessTime = millis();
}

// 修改loop函数中的周期控制部分
void loop() {
    static unsigned long lastNMEACycle = 0;
    static bool nmeaActive = false;
    
    unsigned long currentTime = millis();
    
    // NMEA周期控制
    if (currentTime - lastNMEACycle >= CYCLE_TOTAL) {
        lastNMEACycle = currentTime;
        nmeaActive = true;
        Serial.println("开始新的10秒周期，打开NMEA");
        sendATCommand("AT+MGPSGET=ALL,1", nullptr, 1000);
    } else if (nmeaActive && (currentTime - lastNMEACycle >= NMEA_ACTIVE)) {
        nmeaActive = false;
        Serial.println("NMEA采集完成，关闭NMEA");
        sendATCommand("AT+MGPSGET=ALL,0", nullptr, 1000);
    }

    // 处理串口数据
    while (SerialAT.available()) {
        char c = SerialAT.read();
        if (c == '\n' || c == '\r') {
            if (serialBuffer.length() > 0) {
                processMQTTMessage(serialBuffer);
                serialBuffer = "";
            }
        } else {
            // 限制缓冲区大小
            if (serialBuffer.length() < 128) {
                serialBuffer += c;
            } else {
                // 缓冲区溢出，重置所有状态
                serialBuffer = "";
                msgState.inJsonMessage = false;
                msgState.inNMEAMessage = false;
                msgState.buffer = "";
            }
        }
    }
    
    // GPS消息队列处理
    if (!nmeaActive && (currentTime - lastMQTTSend >= MQTT_SEND_INTERVAL)) {
        char jsonData[96];
        if (dequeueGPS(jsonData)) {
            char mqttMessage[192];
            snprintf(mqttMessage, sizeof(mqttMessage), 
                    "AT+MPUB=\"%s\",0,0,\"%s\"", 
                    TOPIC_PUB_GPS, jsonData);
            sendATCommand(mqttMessage, nullptr, 1000);
            lastMQTTSend = currentTime;
        }
    }
}
