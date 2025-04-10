# 4G智能定位开放平台接口文档

## 第三方扩展说明

提供以下开放接口供开发者扩展：
- 设备类型注册接口
- 定位数据订阅接口
- 指令透传通道接口
- 报警规则配置接口


## 核心功能接口

### 1. 用户认证

### 用户登录
- **请求方法**: POST
- **路径**: `/api/login`
- **请求参数**:
  ```json
  {
    "username": "用户名",
    "password": "密码"
  }
  ```
- **返回格式**:
  ```json
  {
    "token": "JWT令牌",
    "userId": "用户ID"
  }
  ```

### 用户登出
- **请求方法**: POST
- **路径**: `/api/logout`
- **鉴权要求**: 需要有效的JWT令牌
- **返回格式**:
  ```json
  {
    "success": true
  }
  ```

## 2. 设备控制

### 发送AT命令
- **请求方法**: POST
- **路径**: `/api/command`
- **鉴权要求**: 需要有效的JWT令牌
- **请求参数**:
  ```json
  {
    "command": "AT命令"
  }
  ```
- **返回格式**:
  ```json
  {
    "success": true,
    "message": "命令已加入队列，将在下次位置更新时发送",
    "queuePosition": 队列位置
  }
  ```

## 3. 数据查询

### 获取位置历史
- **请求方法**: GET
- **路径**: `/api/locations`
- **鉴权要求**: 需要有效的JWT令牌
- **返回格式**:
  ```json
  [
    {
      "id": 记录ID,
      "device_id": 设备ID,
      "longitude": 经度,
      "latitude": 纬度,
      "altitude": 高度,
      "created_at": 时间戳
    },
    ...
  ]
  ```

### 获取命令历史
- **请求方法**: GET
- **路径**: `/api/commands`
- **鉴权要求**: 需要有效的JWT令牌
- **返回格式**:
  ```json
  [
    {
      "id": 记录ID,
      "device_id": 设备ID,
      "command": "AT命令",
      "result": "命令结果",
      "created_at": 时间戳
    },
    ...
  ]
  ```

### 获取设备列表
- **请求方法**: GET
- **路径**: `/api/devices`
- **鉴权要求**: 需要有效的JWT令牌
- **返回格式**:
  ```json
  [
    {
      "id": 设备ID,
      "client_id": "设备客户端ID",
      "user_id": 用户ID
    }
  ]
  ```

## 4. WebSocket连接
### 4.1 连接地址
- `ws://localhost:5000`

### 4.2 连接建立流程
1. 客户端发起WebSocket连接请求
2. 服务器验证连接并返回200状态码
3. 连接建立成功后，服务器会定期发送心跳包(ping)
4. 客户端需要响应心跳包(pong)以保持连接

### 4.3 消息格式规范
#### 请求消息格式
```json
{
  "action": "subscribe/unsubscribe",
  "topics": ["AT_GPS_result", "4G"]
}
```

#### 响应消息格式
```json
{
  "status": true/false,
  "message": "操作结果描述"
}
```

### 4.4 数据推送格式
```json
{
  "topic": "消息主题(AT_GPS_result或4G)",
  "data": {
    // 根据主题不同，data结构不同
  }
}
```

### 4.5 详细数据格式
#### AT_GPS_result主题数据
```json
{
  "topic": "AT_GPS_result",
  "data": {
    "status": true/false,
    "line1": "结果行1",
    "line2": "结果行2",
    // ...最多到line10
  }
}
```

#### 4G主题数据
```json
{
  "topic": "4G",
  "data": {
    "device_id": "设备ID",
    "timestamp": "2023-01-01T12:00:00Z",
    "latitude": 39.9042,
    "longitude": 116.4074,
    "bd09_latitude": 39.9042,
    "bd09_longitude": 116.4074
  }
}
```

### 4.6 实时数据推送机制
1. **位置更新推送**：当设备通过4G主题发送GPS数据时，服务器会将转换后的BD09坐标一并推送到客户端
2. **AT命令结果推送**：当设备通过AT_GPS_result主题返回命令执行结果时，服务器会实时推送到客户端
3. **连接管理**：客户端断开连接后会自动清理资源

### 4.7 错误处理
1. **连接错误**：返回HTTP状态码和错误信息
2. **消息格式错误**：返回错误JSON格式
   ```json
   {
     "error": "Invalid message format",
     "code": 400
   }
   ```
3. **心跳超时**：30秒内未收到pong响应则断开连接

### 4.8 示例
#### 订阅请求示例
```json
{
  "action": "subscribe",
  "topics": ["4G", "AT_GPS_result"]
}
```

#### 位置数据推送示例
```json
{
  "topic": "4G",
  "data": {
    "device_id": "DEV12345",
    "timestamp": "2023-01-01T12:00:00Z",
    "latitude": 39.9042,
    "longitude": 116.4074,
    "bd09_latitude": 39.9042,
    "bd09_longitude": 116.4074
  }
}
```

## 5. 数据库设计
### 表结构说明
#### users表
- id: 用户ID，自增主键
- username: 用户名，唯一
- password: 密码
- token: JWT令牌

#### devices表
- id: 设备ID，自增主键
- client_id: 设备标识，唯一
- user_id: 关联用户ID
- name: 设备名称

#### command_history表
- id: 记录ID，自增主键
- device_id: 关联设备ID
- command: AT命令内容
- result: 命令执行结果
- created_at: 记录时间

#### location_history表
- id: 记录ID，自增主键
- device_id: 关联设备ID
- longitude: 经度
- latitude: 纬度
- altitude: 海拔
- created_at: 记录时间

### 表关系
1. users(1) -> devices(*): 一对多关系
2. devices(1) -> command_history(*): 一对多关系
3. devices(1) -> location_history(*): 一对多关系

- **连接地址**: `ws://localhost:5000`
- **消息格式**:
  ```json
  {
    "topic": "消息主题(AT_GPS_result或4G)",
    "data": 消息数据
  }
  ```

## 6. 数据格式规范
### MQTT主题数据格式
#### AT_GPS主题
用于向设备发送AT命令
```json
{
  "client_id":"设备ID",
  "at_cmd":"AT命令"
}
```

#### AT_GPS_result主题
设备返回AT命令执行结果
```json
{
  "status": true/false,
  "line1": "结果行1",
  "line2": "结果行2",
  // ...最多到line10
}
```

#### 4G主题
设备返回的定位数据
成功时:
```json
{
  "status": 1,
  "cn": 1,
  "wgs84": {
    "longitude": 经度,
    "latitude": 纬度,
    "altitude": 高度
  },
  "bd09": {
    "longitude": 百度坐标经度,
    "latitude": 百度坐标纬度
  }
}
```
失败时返回`null`

### 坐标转换规则
服务端会自动将wgs84坐标转换为bd09坐标并添加到返回数据中。

### 数据库存储规则
所有接收到的数据都会存储到数据库，包括:
- AT命令发送记录
- AT命令执行结果
- 设备定位数据