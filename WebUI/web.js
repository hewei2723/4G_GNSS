let token = localStorage.getItem('token');

const API_BASE = 'http://后端接口地址';

let map, marker; // 全局地图及标记变量
const positions = [];

// 快捷命令功能
function quickCommand(cmd) {
    document.getElementById('command').value = cmd;
    sendCommand();
}

// 优化登录错误提示
async function login() {
    const username = document.getElementById('username').value;
    const password = document.getElementById('password').value;
    const errorElement = document.getElementById('login-error');

    if (!username || !password) {
        errorElement.textContent = '请输入用户名和密码';
        return;
    }

    try {
        console.log('[登录请求发送]', { username });
        
        const response = await fetch(`${API_BASE}/login`, {
            method: 'POST',
            headers: { 
                'Content-Type': 'application/json'
            },
            body: JSON.stringify({ username, password })
        });

        const data = await response.json();
        console.log('[登录响应]', { 
            status: response.status,
            ok: response.ok,
            data
        });

        if (response.ok && data.token) {
            token = data.token;
            localStorage.setItem('token', token);
            showMainInterface();
            errorElement.textContent = '';
        } else {
            errorElement.textContent = data.error || '登录失败';
            console.error('登录失败:', data);
        }
    } catch (error) {
        console.error('登录错误:', error);
        errorElement.textContent = '服务器连接失败';
    }
}

async function sendCommand(command = null) {
    if (!command) {
        command = document.getElementById('command').value;
    }
    console.log('[前端] 发送命令:', command); // 添加日志

    // 移除命令字符串中的空格
    command = command.replace(/\s/g, '');

    try {
        const response = await fetch(`${API_BASE}/command`, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
                'Authorization': `Bearer ${token}`
            },
            body: JSON.stringify({ command })
        });

        console.log('[前端] 命令发送响应:', response); // 添加日志

        if (!response.ok) {
            throw new Error(`HTTP error! status: ${response.status}`);
        }

        const data = await response.json();
        console.log('[前端] 命令发送成功:', data);
    } catch (error) {
        console.error('[前端] 命令发送失败:', error);
        alert('命令发送失败');
    }
}

function showMainInterface() {
    document.getElementById('login').style.display = 'none';
    document.getElementById('main').style.display = 'flex';
    initMap(); // 初始化交互式地图
    initWebSocket();
    loadDevices();
    load4GMessages(); // 初次加载
    setInterval(load4GMessages, 5000); // 每5秒刷新一次4G消息
}

function initMap() {
    const container = document.getElementById('map');
    container.innerHTML = ''; // 清空容器
    // 初始化百度地图，默认中心坐标可取 113.89,35.21
    map = new BMapGL.Map("map");
    let defaultPoint = new BMapGL.Point(113.89, 35.21);
    map.centerAndZoom(defaultPoint, 13);
    map.enableScrollWheelZoom(true);
    // 新建标记并添加到地图
    marker = new BMapGL.Marker(defaultPoint);
    map.addOverlay(marker);
}

function updateMap(bd09) {
    // 通过返回的 bd09 坐标更新地图
    if (bd09 && bd09.longitude && bd09.latitude) {
        let point = new BMapGL.Point(bd09.longitude, bd09.latitude);
        if (marker) {
            marker.setPosition(point);
        } else {
            marker = new BMapGL.Marker(point);
            map.addOverlay(marker);
        }
        // 可选：平滑移动地图中心
        map.panTo(point);
    }
}

function initWebSocket() {
    const ws = new WebSocket('wss://后端接口地址');
    
    ws.onmessage = (event) => {
        const { topic, data } = JSON.parse(event.data);
        
        if (topic === '4G' && data && data.status === 1) {
            updateLocationInfo(data);
            updateMap(data.bd09); // 根据 bd09 坐标更新地图
            appendMqttLog(`[4G] ${JSON.stringify(data)}`);
        } else if (topic === 'AT_GPS_result') {
            updateCommandResult(data);
        }
    };

    ws.onclose = () => {
        setTimeout(() => {
            console.log('正在重新连接WebSocket...');
            initWebSocket();
        }, 3000);
    };

    ws.onerror = (error) => {
        console.error('WebSocket error:', error);
    };
}

async function loadDevices() {
    try {
        const response = await fetch(`${API_BASE}/devices`, {
            headers: {
                'Authorization': `Bearer ${token}`
            }
        });
        const devices = await response.json();
        // 显示设备列表
        document.getElementById('device-status').innerHTML = 
            devices.map(device => `
                <div>设备ID: ${device.client_id}</div>
                <div>设备名称: ${device.name}</div>
            `).join('');
    } catch (error) {
        console.error('加载设备列表失败:', error);
    }
}

// 优化位置信息显示
function updateLocationInfo(data) {
    const {altitude } = data.wgs84;
    const { longitude, latitude} = data.bd09;
    document.getElementById('location-info').innerHTML = `
        <div class="info-grid">
            <div><strong>经度:</strong> ${longitude.toFixed(6)}°</div>
            <div><strong>纬度:</strong> ${latitude.toFixed(6)}°</div>
            <div><strong>更新时间:</strong> ${new Date().toLocaleTimeString()}</div>
        </div>
    `;
}

// 优化命令结果显示
function updateCommandResult(data) {
    const resultDiv = document.getElementById('command-result');
    const lines = Object.entries(data)
        .filter(([key, value]) => key.startsWith('line') && value)
        .map(([_, value]) => value);
    
    resultDiv.innerHTML = `
        <div class="command-response">
            <div class="timestamp">${new Date().toLocaleTimeString()}</div>
            <pre>${lines.join('\n')}</pre>
        </div>
        ${resultDiv.innerHTML}
    `;
}

function logout() {
    // 清除本地token
    token = null;
    localStorage.removeItem('token');
    // 切换到登录界面
    document.getElementById('main').style.display = 'none';
    document.getElementById('login').style.display = 'block';
}

// 初始化检查登录状态
if (token) {
    showMainInterface();
} else {
    document.getElementById('login').style.display = 'block';
}
