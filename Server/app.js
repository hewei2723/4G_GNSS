const express = require('express');
const jwt = require('jsonwebtoken');
const bcrypt = require('bcrypt');
const cors = require('cors');
const mqtt = require('mqtt');
const mysql = require('mysql2/promise');
const WebSocket = require('ws');
const fetch = require('node-fetch'); // 新增：引入 node-fetch，用于请求百度静态地图 API
const gcoord = require('gcoord'); // 新增：引入 gcoord 库
const cluster = require('cluster');
const os = require('os');

// 数据库配置
const dbConfig = {
    host: '数据库地址',
    user: '数据库用户名',
    password: '数据库密码',
    database: '数据库名称',
    waitForConnections: true,
    connectionLimit: 10
};

// 创建数据库连接池
const pool = mysql.createPool(dbConfig);

// MQTT客户端配置
let mqttClient;

if (cluster.isMaster) {
    // 主进程创建MQTT客户端
    mqttClient = mqtt.connect('mqtt://127.0.0.1:1883', {
        username: 'mqtt用户名',
        password: 'mqtt密码',
        clean: true,
        keepalive: 60
    });

    // 监听工作进程消息
    cluster.on('message', (worker, message) => {
        if (message.type === 'mqtt_publish') {
            mqttClient.publish(message.topic, message.payload, message.options);
        }
    });

    // 将MQTT消息转发给所有工作进程
    mqttClient.on('message', (topic, message) => {
        for (const id in cluster.workers) {
            cluster.workers[id].send({
                type: 'mqtt_message',
                topic,
                message: message.toString()
            });
        }
    });
} else {
    // 工作进程通过IPC与主进程通信
    process.on('message', (message) => {
        if (message.type === 'mqtt_message') {
            // 模拟MQTT消息事件
            mqttClient.emit('message', message.topic, message.message);
        }
    });

    // 创建代理MQTT客户端
    mqttClient = {
        publish: (topic, payload, options) => {
            process.send({
                type: 'mqtt_publish',
                topic,
                payload,
                options
            });
        },
        on: (event, callback) => {
            if (event === 'message') {
                mqttClient._messageCallback = callback;
            }
        },
        emit: (event, ...args) => {
            if (event === 'message' && mqttClient._messageCallback) {
                mqttClient._messageCallback(...args);
            }
        }
    };
}

const app = express();
app.use(express.json());
app.use(cors());

const JWT_SECRET = 'jwt认证密钥';
const clients = new Set();
let g4Messages = []; 
let commandQueue = []; 
let enableQueueSending = false; // 新增：控制队列发送的开关
let pendingCommandResponse = null; // 新增：存储待处理的命令响应

// 清空队列函数
function clearCommandQueue() {
    commandQueue = [];
    console.log('[命令队列] 已清空');
}

// MQTT连接和订阅
mqttClient.on('connect', () => {
    console.log('[MQTT] 连接成功');
    mqttClient.subscribe(['AT_GPS_result', '4G', 'AT_GPS']); // 增加AT_GPS订阅，用于调试确认设备是否接收命令
    clearCommandQueue(); // 连接时清空命令队列
});

// MQTT消息处理
mqttClient.on('message', async (topic, message) => {
    try {
        const messageStr = message.toString();
        let data = JSON.parse(messageStr);
        console.log(`[MQTT] 收到消息 - Topic: ${topic}, Message: ${messageStr}`);
        
        // 当是4G主题消息时，进行处理
        if (topic === '4G' && data && data.status === 1) {
            // 使用已定义的convertToBd09函数进行坐标转换
            const bd09 = convertToBd09(
                parseFloat(data.wgs84.longitude), 
                parseFloat(data.wgs84.latitude)
            );
            
            // 修改：不使用toFixed，保持数值类型
            data.bd09 = {
                longitude: bd09[0],
                latitude: bd09[1]
            };

            // 更新消息数组
            g4Messages.push({ data, timestamp: new Date().toISOString() });
            if (g4Messages.length > 100) {
                g4Messages.shift();
            }

            // 处理GPS位置数据存储
            const [device] = await pool.execute(
                'SELECT id FROM devices WHERE client_id = ?',
                ['4G_GPS']
            );
            if (device[0]) {
                await pool.execute(
                    'INSERT IGNORE INTO location_history (device_id, longitude, latitude, altitude) VALUES (?, ?, ?, ?)',
                    [device[0].id, data.wgs84.longitude, data.wgs84.latitude, data.wgs84.altitude]
                );
            }

            // 广播完整数据（包含转换后的bd09坐标）到WebSocket客户端
            const wsMessage = JSON.stringify({ topic, data });
            clients.forEach(client => {
                if (client.readyState === WebSocket.OPEN) {
                    client.send(wsMessage);
                }
            });

            // 位置更新后，立即处理命令队列
            if (commandQueue.length > 0) {
                const nextCommand = commandQueue.shift();
                // 构建多行格式的JSON字符串，确保格式正确
                const mqttMessage = `{\n  \"client_id\": \"4G_GPS\",\n  \"at_cmd\": \"${nextCommand.command}\"\n}`;
                console.log(`[调试] 准备发送MQTT消息到AT_GPS主题:`, mqttMessage);
                
                mqttClient.publish('AT_GPS', mqttMessage, {
                    qos: 1,
                    retain: false,  // 修改为false
                    dup: false
                }, async (err) => {
                    if (err) {
                        console.error('[MQTT发送错误]', err);
                    } else {
                        console.log('[MQTT] 消息发送成功');
                        // 记录命令历史
                        if (nextCommand.deviceId) {
                            await pool.execute(
                                'INSERT INTO command_history (device_id, command) VALUES (?, ?)',
                                [nextCommand.deviceId, nextCommand.command]
                            );
                        }
                    }
                });
            }
        }

        // 处理AT命令结果
        if (topic === 'AT_GPS_result') {
            const [device] = await pool.execute(
                'SELECT id FROM devices WHERE client_id = ?',
                ['4G_GPS']
            );
            if (device[0]) {
                await pool.execute(
                    'INSERT INTO command_history (device_id, result) VALUES (?, ?)',
                    [device[0].id, JSON.stringify(data)]
                );
            }

            // 如果有待处理的命令响应，返回给客户端
            if (pendingCommandResponse) {
                pendingCommandResponse.json({ success: true, result: data });
                pendingCommandResponse = null;
            }

            // 广播AT命令结果到WebSocket客户端
            const wsMessage = JSON.stringify({ topic, data });
            clients.forEach(client => {
                if (client.readyState === WebSocket.OPEN) {
                    client.send(wsMessage);
                }
            });
        }

        // 调试日志：收到AT_GPS主题的消息
        if(topic === 'AT_GPS') {
            console.log('[调试] 收到AT_GPS主题的消息，内容:', message.toString());
        }

    } catch (error) {
        // 只记录非重复键错误
        if (error.code !== 'ER_DUP_ENTRY') {
            console.error('[MQTT错误]', error);
        }
    }
});

// Token验证中间件
// 用户会话存储
const userSessions = new Map();

const authenticateToken = async (req, res, next) => {
    const token = req.headers['authorization']?.split(' ')[1];
    if (!token) return res.status(401).json({ error: '未提供认证令牌' });

    try {
        const user = jwt.verify(token, JWT_SECRET);
        
        // 检查会话是否超时
        const session = userSessions.get(user.id);
        if (session && Date.now() - session.lastActivity > 30 * 60 * 1000) {
            userSessions.delete(user.id);
            return res.status(401).json({ error: '会话已超时，请重新登录' });
        }

        // 更新最后活动时间
        userSessions.set(user.id, { lastActivity: Date.now() });
        req.user = user;
        next();
    } catch {
        return res.status(403).json({ error: '无效的认证令牌' });
    }
};

// API路由
// 用户登录
app.post('/api/login', async (req, res) => {
    const { username, password } = req.body;
    try {
        const [users] = await pool.execute(
            'SELECT * FROM users WHERE username = ?',
            [username]
        );

        if (users.length && password === users[0].password) {
            const token = jwt.sign({ id: users[0].id }, JWT_SECRET);
            res.json({ token, userId: users[0].id });
        } else {
            res.status(401).json({ error: '用户名或密码错误' });
        }
    } catch (error) {
        res.status(500).json({ error: '服务器错误' });
    }
});

// 新增登出接口
app.post('/api/logout', authenticateToken, (req, res) => {
    res.json({ success: true });
});

// 修改发送AT命令的接口
app.post('/api/command', authenticateToken, async (req, res) => {
    const { command } = req.body;
    try {
        const [device] = await pool.execute(
            'SELECT id FROM devices WHERE client_id = ?',
            ['4G_GPS']
        );

        if (!device[0]) {
            console.log('[命令发送] 设备未找到');
            return res.status(404).json({ error: '设备未找到' });
        }

        const deviceId = device[0].id;
        console.log(`[命令加入队列] 设备ID: ${deviceId}, 命令: ${command}`);

        // 将命令添加到队列
        commandQueue.push({
            deviceId,
            command,
            timestamp: new Date()
        });

        // 如果队列发送被禁用，直接发送命令
        if (!enableQueueSending) {
            const mqttMessage = `{
  "client_id": "4G_GPS",
  "at_cmd": "${command}"
}`;
            console.log(`[调试] 直接发送MQTT消息到AT_GPS主题:`, mqttMessage);
            
            mqttClient.publish('AT_GPS', mqttMessage, {
                qos: 1,
                retain: false,
                dup: false
            }, (err) => {
                if (err) {
                    console.error('[MQTT发送错误]', err);
                    return res.status(500).json({ error: '命令发送失败' });
                } else {
                    console.log('[MQTT] 消息发送成功');
                }
            });

            // 记录命令历史
            await pool.execute(
                'INSERT INTO command_history (device_id, command) VALUES (?, ?)', 
                [deviceId, command]
            );

            // 存储待处理的命令响应
            pendingCommandResponse = res;
        } else {
            res.json({ 
                success: true, 
                message: '命令已加入队列，将在下次位置更新时发送',
                queuePosition: commandQueue.length
            });
        }
    } catch (error) {
        console.error('[命令队列添加失败]', error);
        res.status(500).json({ error: '命令添加失败' });
    }
});

// 获取位置历史
app.get('/api/locations', authenticateToken, async (req, res) => {
    try {
        const [locations] = await pool.execute(
            'SELECT * FROM location_history WHERE device_id = (SELECT id FROM devices WHERE client_id = ?) ORDER BY created_at DESC LIMIT 100',
            ['4G_GPS']
        );
        res.json(locations);
    } catch (error) {
        res.status(500).json({ error: '获取位置历史失败' });
    }
});

// 获取命令历史
app.get('/api/commands', authenticateToken, async (req, res) => {
    try {
        const [commands] = await pool.execute(
            'SELECT * FROM command_history WHERE device_id = (SELECT id FROM devices WHERE client_id = ?) ORDER BY created_at DESC LIMIT 50',
            ['4G_GPS']
        );
        res.json(commands);
    } catch (error) {
        res.status(500).json({ error: '获取命令历史失败' });
    }
});

// 获取设备列表
app.get('/api/devices', authenticateToken, async (req, res) => {
    try {
        const [devices] = await pool.execute(
            'SELECT * FROM devices WHERE user_id = ?',
            [req.user.id]
        );
        res.json(devices);
    } catch (error) {
        res.status(500).json({ error: '获取设备列表失败' });
    }
});

// 启动服务器
const server = require('http').createServer(app);
const wss = new WebSocket.Server({ server });

wss.on('connection', (ws) => {
    clients.add(ws);
    ws.on('close', () => clients.delete(ws));
});

if (cluster.isMaster) {
    const numCPUs = os.cpus().length;
    console.log(`[聚集模式] 主进程启动，CPU核数: ${numCPUs}`);
    // 分叉工作进程，实现多设备登录
    for (let i = 0; i < numCPUs; i++) {
        cluster.fork();
    }
    // 如果有进程退出则重启
    cluster.on('exit', (worker, code, signal) => {
        console.log(`[聚集模式] 工作进程 ${worker.process.pid} 退出，重启...`);
        cluster.fork();
    });
} else {
    // 工作进程执行启动服务逻辑
    pool.getConnection()
        .then(async connection => {
            console.log(`[工作进程 ${process.pid}] [数据库] 连接成功`);
            connection.release();
            
            // 如果使用集群，需要注意WebSocket粘性会话问题
            server.listen(5000, '0.0.0.0', () => { // 修改监听地址为0.0.0.0
                console.log(`[工作进程 ${process.pid}] [服务启动] 服务器运行在端口 5000`);
            });
        })
        .catch(err => {
            console.error('[启动失败]', err);
            process.exit(1);
        });
}

// 错误处理
process.on('uncaughtException', (error) => {
    console.error('[未捕获的异常]', error);
});

process.on('unhandledRejection', (error) => {
    console.error('[未处理的Promise拒绝]', error);
});

// 百度坐标转换函数
function convertToBd09(lon, lat) {
    const result = gcoord.transform(
        [lon, lat], // 经纬度坐标
        gcoord.WGS84, // 当前坐标系
        gcoord.BD09 // 目标坐标系
    );
    return result;
}
