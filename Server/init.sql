-- 创建用户表
CREATE TABLE IF NOT EXISTS users (
    id INT AUTO_INCREMENT PRIMARY KEY,
    username VARCHAR(50) UNIQUE NOT NULL,
    password VARCHAR(255) NOT NULL,
    token VARCHAR(255)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 创建设备表
CREATE TABLE IF NOT EXISTS devices (
    id INT AUTO_INCREMENT PRIMARY KEY,
    client_id VARCHAR(50) UNIQUE NOT NULL,
    user_id INT,
    name VARCHAR(100),
    FOREIGN KEY (user_id) REFERENCES users(id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 创建AT命令历史记录表
CREATE TABLE IF NOT EXISTS command_history (
    id INT AUTO_INCREMENT PRIMARY KEY,
    device_id INT,
    command TEXT,
    result TEXT,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (device_id) REFERENCES devices(id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 创建GPS位置记录表
CREATE TABLE IF NOT EXISTS location_history (
    id INT AUTO_INCREMENT PRIMARY KEY,
    device_id INT,
    longitude DECIMAL(10, 6),
    latitude DECIMAL(10, 6),
    altitude DECIMAL(10, 6),
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (device_id) REFERENCES devices(id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 创建存储过程，插入唯一位置数据
DELIMITER $$

CREATE PROCEDURE InsertUniqueLocation(
    IN p_device_id INT,
    IN p_longitude DECIMAL(10,6),
    IN p_latitude DECIMAL(10,6),
    IN p_altitude DECIMAL(10,6)
)
BEGIN
    DECLARE last_longitude DECIMAL(10,6);
    DECLARE last_latitude DECIMAL(10,6);
    DECLARE last_altitude DECIMAL(10,6);

    -- 获取最新一条相同设备的数据
    SELECT longitude, latitude, altitude 
    INTO last_longitude, last_latitude, last_altitude
    FROM location_history
    WHERE device_id = p_device_id
    ORDER BY created_at DESC 
    LIMIT 1;

    -- 如果最新的数据与当前要插入的数据完全相同，则不插入
    IF last_longitude IS NOT NULL 
        AND last_latitude IS NOT NULL 
        AND last_altitude IS NOT NULL 
        AND last_longitude = p_longitude 
        AND last_latitude = p_latitude 
        AND last_altitude = p_altitude 
    THEN
        -- 直接结束存储过程，不执行插入
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = '数据重复，不插入';
    ELSE
        -- 插入新数据
        INSERT INTO location_history (device_id, longitude, latitude, altitude) 
        VALUES (p_device_id, p_longitude, p_latitude, p_altitude);
    END IF;

END $$

DELIMITER ;


-- 创建默认设备
INSERT INTO devices (client_id, user_id, name) VALUES 
('4G_GPS', 1, '默认设备');
