let token = localStorage.getItem('token');

const API_BASE = 'https://后端接口地址';

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
            window.location.href = 'console.html';
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

if (token) {
    window.location.href = 'console.html';
}