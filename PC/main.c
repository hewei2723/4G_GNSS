#include <stdio.h>
#include <windows.h>
#include <string.h>
#include <math.h>

#define BUFFER_SIZE 1024
#define DEFAULT_TIMEOUT 1000
#define RX_BUFFER_SIZE 1024

#define PI 3.14159265358979324
#define X_PI (PI * 3000.0 / 180.0)

HANDLE hSerial;
// 串口初始化函数
BOOL initSerialPort(const char *portName)
{
    char comPath[20];
    sprintf(comPath, "\\\\.\\%s", portName);

    hSerial = CreateFile(comPath,
                         GENERIC_READ | GENERIC_WRITE,
                         0,
                         0,
                         OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL,
                         0);

    if (hSerial == INVALID_HANDLE_VALUE)
    {
        printf("打开串口失败，错误代码: %d\n", GetLastError());
        return FALSE;
    }

    // 设置串口超时参数
    COMMTIMEOUTS timeouts = {0};
    timeouts.ReadIntervalTimeout = 50;
    timeouts.ReadTotalTimeoutConstant = 50;
    timeouts.ReadTotalTimeoutMultiplier = 10;
    timeouts.WriteTotalTimeoutConstant = 50;
    timeouts.WriteTotalTimeoutMultiplier = 10;
    if (!SetCommTimeouts(hSerial, &timeouts))
    {
        printf("设置超时参数失败\n");
        CloseHandle(hSerial);
        return FALSE;
    }

    DCB dcbSerialParams = {0};
    dcbSerialParams.DCBlength = sizeof(dcbSerialParams);

    if (!GetCommState(hSerial, &dcbSerialParams))
    {
        printf("获取串口状态失败\n");
        CloseHandle(hSerial);
        return FALSE;
    }

    dcbSerialParams.BaudRate = CBR_115200;
    dcbSerialParams.ByteSize = 8;
    dcbSerialParams.StopBits = ONESTOPBIT;
    dcbSerialParams.Parity = NOPARITY;
    
    // 添加额外的串口控制设置
    dcbSerialParams.fBinary = TRUE;
    dcbSerialParams.fDtrControl = DTR_CONTROL_ENABLE;
    dcbSerialParams.fRtsControl = RTS_CONTROL_ENABLE;
    dcbSerialParams.fOutxCtsFlow = FALSE;
    dcbSerialParams.fOutxDsrFlow = FALSE;
    dcbSerialParams.fDsrSensitivity = FALSE;
    dcbSerialParams.fAbortOnError = FALSE;

    if (!SetCommState(hSerial, &dcbSerialParams))
    {
        printf("设置串口参数失败\n");
        CloseHandle(hSerial);
        return FALSE;
    }

    // 清空缓冲区
    if (!PurgeComm(hSerial, PURGE_RXCLEAR | PURGE_TXCLEAR))
    {
        printf("清空缓冲区失败\n");
        CloseHandle(hSerial);
        return FALSE;
    }

    // 设置DTR和RTS信号
    EscapeCommFunction(hSerial, SETDTR);
    EscapeCommFunction(hSerial, SETRTS);
    
    Sleep(200);  // 等待串口稳定
    
    return TRUE;
}

// AT命令发送和接收函数
BOOL sendATCommand(const char *command, char *response, int timeout)
{
    DWORD bytesWritten, bytesRead;
    char cmdBuf[BUFFER_SIZE];
    char rxBuffer[RX_BUFFER_SIZE] = {0};

    // 清空接收缓冲区
    PurgeComm(hSerial, PURGE_RXCLEAR | PURGE_TXCLEAR);

    // 发送AT命令
    sprintf(cmdBuf, "%s\r\n", command);
    if (!WriteFile(hSerial, cmdBuf, strlen(cmdBuf), &bytesWritten, NULL))
    {
        return FALSE;
    }

    // 等待并读取响应
    Sleep(timeout);
    if (ReadFile(hSerial, rxBuffer, RX_BUFFER_SIZE - 1, &bytesRead, NULL))
    {
        if (response != NULL)
        {
            strcpy(response, rxBuffer);
        }
        return TRUE;
    }
    return FALSE;
}

// 添加GPS数据提取函数
void extractGPSData(const char *response, char *gpsData)
{
    const char *start = strstr(response, "+GPSST:");
    if (start)
    {
        // 找到+GPSST:后的数据
        start += 7; // 跳过"+GPSST:"
        const char *end = strstr(start, "\r\n");
        if (end)
        {
            int len = end - start;
            strncpy(gpsData, start, len);
            gpsData[len] = '\0';
        }
        else
        {
            strcpy(gpsData, start);
        }
    }
    else
    {
        gpsData[0] = '\0';
    }
}

// WGS84转BD09坐标系转换函数
void wgs84_to_bd09(double lat, double lon, double *bd_lat, double *bd_lon) {
    double x = lon, y = lat;
    double z = sqrt(x * x + y * y) + 0.00002 * sin(y * X_PI);
    double theta = atan2(y, x) + 0.000003 * cos(x * X_PI);
    *bd_lon = z * cos(theta) + 0.0065;
    *bd_lat = z * sin(theta) + 0.006;
}

// GPS数据解析和JSON格式化函数
void parseGPSToJSON(const char *gpsData, char *jsonOutput) {
    int status, cn;
    double longitude, altitude, latitude;
    
    sscanf(gpsData, "%d, %d, %lf, %lf, %lf", &status, &cn, &longitude, &altitude, &latitude);
    
    double bd_lat, bd_lon;
    wgs84_to_bd09(latitude, longitude, &bd_lat, &bd_lon);
    
    sprintf(jsonOutput, 
            "{\"status\":%d,\"cn\":%d,\"wgs84\":{\"longitude\":%.6f,\"latitude\":%.6f,\"altitude\":%.6f},"
            "\"bd09\":{\"longitude\":%.6f,\"latitude\":%.6f}}",
            status, cn, longitude, latitude, altitude, bd_lon, bd_lat);
}

// 串口读取函数
void readFromSerialPort(HANDLE hSerial)
{
    char buffer[BUFFER_SIZE];
    DWORD bytesRead;
    if (ReadFile(hSerial, buffer, BUFFER_SIZE - 1, &bytesRead, NULL))
    {
        buffer[bytesRead] = '\0';
        printf("串口回显: %s\n", buffer);
    }
}

// 添加转换自 esp32.ino 的初始化配置函数
void initModem() {
    printf("发送AT指令，设置GPRS参数\n");
    sendATCommand("AT+QICSGP=1,1,\"\",\"\",\"\"", NULL, 1000);

    printf("打开GPRS连接\n");
    sendATCommand("AT+NETOPEN", NULL, 2000);

    printf("设置NTP服务器\n");
    sendATCommand("AT+QNTP=1,\"tms.dynamicode.com.cn\",123,1", NULL, 500);

    printf("设置MCONFIG参数\n");
    sendATCommand("AT+MCONFIG=\"4G_GPS\",\"4G_GPS\",898604E6192391567777", NULL, 500);

    printf("设置MIPSTART参数\n");
    sendATCommand("AT+MIPSTART=\"mqtt.lttac.cn\",1883", NULL, 500);

    printf("连接MQTT服务器\n");
    sendATCommand("AT+MCONNECT=1,60", NULL, 1000);

    printf("订阅主题\n");
    sendATCommand("AT+MSUB=\"AT_GPS\",0", NULL, 1000);

    printf("添加结果反馈主题\n");
    sendATCommand("AT+MPUB=\"AT_GPS_result\",0,0,\"初始化完成\"", NULL, 1000);

    printf("开启GPS\n");
    sendATCommand("AT+MGPSC=1", NULL, 1000);

    printf("设置热启动\n");
    sendATCommand("AT+GPSMODE=1", NULL, 1000);

    printf("发布消息：等待获取GPS信息\n");
    sendATCommand("AT+MPUB=\"4G\",0,0,\"等待获取GPS信息\"", NULL, 1000);

    // 修改这里，将原来的关闭NMEA改为开启NMEA数据输出
    printf("开启NMEA消息\n");
    sendATCommand("AT+MGPSGET=ALL,1", NULL, 1000);
}

// 新增函数：解析NMEA中$GNGGA语句并生成JSON
void parseNMEAGGAToJSON(const char* nmea, char* jsonOutput) {
    // 复制一份可修改的内容
    char nmeaCopy[BUFFER_SIZE];
    strncpy(nmeaCopy, nmea, BUFFER_SIZE-1);
    nmeaCopy[BUFFER_SIZE-1] = '\0';

    // 使用 strtok 拆分逗号字段
    char *token = strtok(nmeaCopy, ",");
    if (!token || strcmp(token, "$GNGGA") != 0) {
        strcpy(jsonOutput, "{\"status\":0}");
        return;
    }
    // 字段顺序: $GNGGA,UTC,lat,NS,lon,EW,fix,cn,hdop,alt,altUnit,...
    token = strtok(NULL, ","); // UTC（忽略）
    char *latStr = strtok(NULL, ",");   // latitude（格式：ddmm.mmmmmm）
    char *ns = strtok(NULL, ",");         // N/S
    char *lonStr = strtok(NULL, ",");     // longitude (dddmm.mmmmmm)
    char *ew = strtok(NULL, ",");         // E/W
    char *fixQuality = strtok(NULL, ","); // fix状态
    char *cnStr = strtok(NULL, ",");      // 卫星数量
    strtok(NULL, ","); // hdop
    char *altStr = strtok(NULL, ",");     // 海拔
    // 检查 fixQuality 有效性
    int fix = atoi(fixQuality);
    int cn = atoi(cnStr);
    double altitude = atof(altStr);
    double latDegrees = 0, lonDegrees = 0;
    // 将纬度转换为度
    if(latStr && strlen(latStr)>=4) {
        char degPart[3] = {0};
        strncpy(degPart, latStr, 2);
        double deg = atof(degPart);
        double min = atof(latStr + 2);
        latDegrees = deg + min/60.0;
        if(ns && (ns[0]=='S' || ns[0]=='s')) {
            latDegrees = -latDegrees;
        }
    }
    // 将经度转换为度
    if(lonStr && strlen(lonStr)>=5) {
        char degPart[4] = {0};
        strncpy(degPart, lonStr, 3);
        double deg = atof(degPart);
        double min = atof(lonStr + 3);
        lonDegrees = deg + min/60.0;
        if(ew && (ew[0]=='W' || ew[0]=='w')) {
            lonDegrees = -lonDegrees;
        }
    }
    
    int status = (fix > 0) ? 1 : 0;
    double bd_lat, bd_lon;
    wgs84_to_bd09(latDegrees, lonDegrees, &bd_lat, &bd_lon);

    sprintf(jsonOutput,
        "{\"status\":%d,\"cn\":%d,\"wgs84\":{\"longitude\":%.6f,\"latitude\":%.6f,\"altitude\":%.2f},"
        "\"bd09\":{\"longitude\":%.6f,\"latitude\":%.6f}}",
        status, cn, lonDegrees, latDegrees, altitude, bd_lon, bd_lat);
}

// 新增函数：解析 NMEA 中 $GNRMC 语句生成 JSON
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
    strtok(NULL, ","); // UTC 时间，忽略
    char *statusStr = strtok(NULL, ","); // 状态 A=有效 V=无效
    char *latStr = strtok(NULL, ",");      // 纬度 (ddmm.mmmmmm)
    char *ns = strtok(NULL, ",");            // N/S
    char *lonStr = strtok(NULL, ",");      // 经度 (dddmm.mmmmmm)
    char *ew = strtok(NULL, ",");            // E/W

    int valid = (statusStr && statusStr[0] == 'A') ? 1 : 0;
    double latDegrees = 0, lonDegrees = 0;
    if(latStr && strlen(latStr) >= 4) {
        char degPart[3] = {0};
        strncpy(degPart, latStr, 2);
        double deg = atof(degPart);
        double min = atof(latStr + 2);
        latDegrees = deg + min / 60.0;
        if(ns && (ns[0]=='S' || ns[0]=='s'))
            latDegrees = -latDegrees;
    }
    if(lonStr && strlen(lonStr) >= 5) {
        char degPart[4] = {0};
        strncpy(degPart, lonStr, 3);
        double deg = atof(degPart);
        double min = atof(lonStr + 3);
        lonDegrees = deg + min / 60.0;
        if(ew && (ew[0]=='W' || ew[0]=='w'))
            lonDegrees = -lonDegrees;
    }
    
    double bd_lat, bd_lon;
    wgs84_to_bd09(latDegrees, lonDegrees, &bd_lat, &bd_lon);
    
    // RMC 中不包含海拔数据，设为 0.00
    sprintf(jsonOutput,
        "{\"status\":%d,\"wgs84\":{\"longitude\":%.6f,\"latitude\":%.6f,\"altitude\":0.00},"
        "\"bd09\":{\"longitude\":%.6f,\"latitude\":%.6f}}",
        valid, lonDegrees, latDegrees, bd_lon, bd_lat);
}

// 修改 processIncomingMessages()：按行拆分，处理包含$GNGGA的NMEA数据
// 全局定时器变量
DWORD lastNMEAOpen = 0;

void processIncomingMessages() {
    static DWORD lastNMEAProcessTime = 0;
    char rxBuffer[RX_BUFFER_SIZE] = {0};
    DWORD bytesRead = 0;
    if (ReadFile(hSerial, rxBuffer, RX_BUFFER_SIZE - 1, &bytesRead, NULL)) {
        if (bytesRead > 0) {
            rxBuffer[bytesRead] = '\0';
            char *line = strtok(rxBuffer, "\r\n");
            while(line != NULL) {
                // 如果收到 $GNGGA 或 $GNRMC 数据，且离上次处理超过2000ms，则解析发送
                if ((strncmp(line, "$GNGGA", 6) == 0 || strncmp(line, "$GNRMC", 6) == 0) &&
                    (GetTickCount() - lastNMEAProcessTime >= 2000)) {
                    char jsonData[RX_BUFFER_SIZE] = {0};
                    if (strncmp(line, "$GNGGA", 6) == 0)
                        parseNMEAGGAToJSON(line, jsonData);
                    else
                        parseNMEARMCToJSON(line, jsonData);
                    printf("解析NMEA JSON: %s\n", jsonData);
                    char mqttMessage[RX_BUFFER_SIZE] = {0};
                    sprintf(mqttMessage, "AT+MPUB=\"4G\",0,0,\"%s\"", jsonData);
                    sendATCommand(mqttMessage, NULL, 1000);
                    lastNMEAProcessTime = GetTickCount();
                }
                else if (strstr(line, "+MSUB: \"AT_GPS\"") != NULL) {
                    printf("检测到新的AT指令消息: %s\n", line);
                    char* cmdStart = strchr(line, '{');
                    if (cmdStart) {
                        printf("解析AT命令: %s\n", cmdStart);
                        handleATCommand(cmdStart);
                    }
                } else {
                    printf("串口回显: %s\n", line);
                }
                line = strtok(NULL, "\r\n");
            }
        }
    }
}

// 用于处理订阅消息中提取的AT命令（简单实现）
void handleATCommand(const char* atCommand) {
    printf("执行AT命令: %s\n", atCommand);
    char response[RX_BUFFER_SIZE] = {0};
    sendATCommand(atCommand, response, 2000);
    printf("AT命令响应: %s\n", response);
}

// 新增全局变量用于周期调度
DWORD cycleStart = 0;
BOOL dataSent = FALSE;

int main()
{
    printf("正在初始化串口...\n");
    if (!initSerialPort("COM6"))
    {
        printf("串口初始化失败\n");
        return 1;
    }
    printf("串口初始化成功\n");

    // 重置设备
    EscapeCommFunction(hSerial, CLRDTR);
    Sleep(200);
    EscapeCommFunction(hSerial, SETDTR);
    Sleep(1000);  // 等待设备重置

    printf("等待模块初始化...\n");
    Sleep(10000);  // 增加等待时间到10秒

    // 测试AT指令响应
    int retryCount = 0;
    const int maxRetries = 10;  // 增加最大重试次数
    BOOL moduleReady = FALSE;

    while (retryCount < maxRetries) {
        printf("\n第 %d 次尝试连接模块...\n", retryCount + 1);
        char response[RX_BUFFER_SIZE] = {0};
        
        // 多次清空串口缓冲区
        PurgeComm(hSerial, PURGE_RXCLEAR | PURGE_TXCLEAR);
        Sleep(500);  // 等待缓冲区清空
        PurgeComm(hSerial, PURGE_RXCLEAR | PURGE_TXCLEAR);
        
        // 发送AT测试指令
        printf("发送AT指令...\n");
        if (sendATCommand("AT", response, 2000)) {  // 增加超时时间
            printf("接收到响应: %s\n", response);
            if (strstr(response, "OK") != NULL) {
                moduleReady = TRUE;
                printf("模块响应正常，初始化成功！\n");
                break;
            } else {
                printf("未收到正确响应\n");
            }
        } else {
            printf("发送AT指令失败\n");
        }
        
        retryCount++;
        Sleep(2000);  // 增加重试间隔到2秒
    }

    if (!moduleReady) {
        printf("模块未就绪，程序退出\n");
        CloseHandle(hSerial);
        return 1;
    }

    // 使用esp32.ino转换后的配置初始化
    initModem();
    
    cycleStart = GetTickCount();

    while (1)
    {
        DWORD t = (GetTickCount() - cycleStart) % 6000; // 6秒周期
        // 3秒时打开NMEA回显
        if (t >= 3000 && t < 3100) { // 防抖保护区间
            sendATCommand("AT+MGPSGET=ALL,1", NULL, 1000);
        }
        // 4秒时处理并发送数据（仅一次）
        if (t >= 4000 && t < 4100 && !dataSent) {
            processIncomingMessages();
            dataSent = TRUE;
        }
        // 5秒时关闭NMEA回显
        if (t >= 5000 && t < 5100) {
            sendATCommand("AT+MGPSGET=ALL,0", NULL, 1000);
        }
        // 周期结束后重置dataSent
        if (t < 100) {
            dataSent = FALSE;
            cycleStart = GetTickCount(); // 新周期
        }
        Sleep(100);
    }
    CloseHandle(hSerial);
    return 0;
}
