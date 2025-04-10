let token = localStorage.getItem('token');

const API_BASE = 'https://后端接口地址';

let map, marker;
const positions = [];

function quickCommand(cmd) {
    document.getElementById('command').value = cmd;
    sendCommand();
}

async function sendCommand(command = null) {
    if (!command) {
        command = document.getElementById('command').value;
    }
    console.log('[前端] 发送命令:', command);

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

        console.log('[前端] 命令发送响应:', response);

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

function initMap() {
    const container = document.getElementById('map');
    container.style.height = 'calc(100vh - 100px)';
    container.innerHTML = '';
    map = new BMapGL.Map("map");
    let defaultPoint = new BMapGL.Point(113.89, 35.21);
    map.centerAndZoom(defaultPoint, 13);
    map.enableScrollWheelZoom(true);
    marker = new BMapGL.Marker(defaultPoint);
    map.addOverlay(marker);
}

function updateMap(bd09) {
    if (bd09 && bd09.longitude && bd09.latitude) {
        let point = new BMapGL.Point(bd09.longitude, bd09.latitude);
        if (marker) {
            marker.setPosition(point);
        } else {
            marker = new BMapGL.Marker(point);
            map.addOverlay(marker);
        }
        map.panTo(point);
    }
}

function initWebSocket() {
    const ws = new WebSocket('wss://后端接口地址');
    
    ws.onmessage = (event) => {
        const { topic, data } = JSON.parse(event.data);
        
        if (topic === '4G' && data && data.status === 1) {
            updateLocationInfo(data);
            updateMap(data.bd09);
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
        document.getElementById('device-status').innerHTML = 
            devices.map(device => `
                <div>设备ID: ${device.client_id}</div>
                <div>设备名称: ${device.name}</div>
            `).join('');
    } catch (error) {
        console.error('加载设备列表失败:', error);
    }
}

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
    token = null;
    localStorage.removeItem('token');
    window.location.href = 'index.html';
}

if (token) {
    initMap();
    initWebSocket();
    loadDevices();
}